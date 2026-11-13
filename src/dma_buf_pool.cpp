#include "dma_buf_pool.hpp"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace logihertz {

void secure_zero_dma_buffer(volatile void *ptr, size_t size) {
    if (!ptr) return;
    volatile uint8_t *p = static_cast<volatile uint8_t *>(ptr);
    while (size--) {
        *p++ = 0;
    }
}

static inline uint32_t align_up_u32(uint32_t val, size_t alignment) {
    if (alignment == 0) return val;
    size_t rem = val % alignment;
    if (rem == 0) return val;
    return val + static_cast<uint32_t>(alignment - rem);
}

static inline size_t align_up_size(size_t val, size_t alignment) {
    if (alignment == 0) return val;
    size_t rem = val % alignment;
    if (rem == 0) return val;
    return val + (alignment - rem);
}

DmaPlaneLayout calculate_dma_surface_layout(
    uint32_t width,
    uint32_t height,
    PixelFormat format,
    double framerate_fps,
    size_t pitch_align,
    size_t page_align
) {
    DmaPlaneLayout layout;
    layout.width = width;
    layout.height = height;
    layout.format = format;
    layout.framerate_fps = (framerate_fps > 0.0) ? framerate_fps : 60.0;

    size_t raw_total_size = 0;

    switch (format) {
        case PixelFormat::NV12: {
            /* NV12: Plane 0 is Luma Y (1 byte/elem), Plane 1 is Chroma UV (2 bytes/elem interleaved, H/2 lines) */
            uint32_t pitch_y = align_up_u32(width, pitch_align);
            layout.pitch_bytes = pitch_y;
            layout.plane_count = 2;

            /* Plane 0 (Y) */
            layout.plane_offsets[0] = 0;
            layout.plane_sizes[0] = pitch_y * height;

            /* Plane 1 (UV) */
            layout.plane_offsets[1] = layout.plane_sizes[0];
            layout.plane_sizes[1] = pitch_y * (height / 2);

            raw_total_size = layout.plane_sizes[0] + layout.plane_sizes[1];
            break;
        }
        case PixelFormat::UYVY: {
            /* UYVY: Packed 4:2:2 (2 bytes/pixel) */
            uint32_t pitch = align_up_u32(width * 2, pitch_align);
            layout.pitch_bytes = pitch;
            layout.plane_count = 1;

            layout.plane_offsets[0] = 0;
            layout.plane_sizes[0] = pitch * height;

            raw_total_size = layout.plane_sizes[0];
            break;
        }
        case PixelFormat::RGBA8: {
            /* RGBA8: Packed 4 bytes/pixel */
            uint32_t pitch = align_up_u32(width * 4, pitch_align);
            layout.pitch_bytes = pitch;
            layout.plane_count = 1;

            layout.plane_offsets[0] = 0;
            layout.plane_sizes[0] = pitch * height;

            raw_total_size = layout.plane_sizes[0];
            break;
        }
        case PixelFormat::PLANAR_RGB_FP32: {
            /* 3 separate planes, 4 bytes/elem */
            uint32_t pitch = align_up_u32(width * 4, pitch_align);
            layout.pitch_bytes = pitch;
            layout.plane_count = 3;

            for (uint32_t c = 0; c < 3; c++) {
                layout.plane_offsets[c] = c * (pitch * height);
                layout.plane_sizes[c] = pitch * height;
            }

            raw_total_size = 3 * (pitch * height);
            break;
        }
        case PixelFormat::PLANAR_RGB_INT8: {
            /* 3 separate planes, 1 byte/elem */
            uint32_t pitch = align_up_u32(width, pitch_align);
            layout.pitch_bytes = pitch;
            layout.plane_count = 3;

            for (uint32_t c = 0; c < 3; c++) {
                layout.plane_offsets[c] = c * (pitch * height);
                layout.plane_sizes[c] = pitch * height;
            }

            raw_total_size = 3 * (pitch * height);
            break;
        }
    }

    /* 4KB ARM SMMU / IOMMU Page Alignment */
    layout.total_aligned_size_bytes = align_up_size(raw_total_size, page_align);

    /* DRAM Bandwidth Calculations in MB/sec */
    double frame_bytes = static_cast<double>(layout.total_aligned_size_bytes);
    double fps = layout.framerate_fps;

    /* Legacy 3-copy pipeline: (1) CSI DMA write to DRAM -> (2) CPU read -> (3) CPU write to staging -> (4) GPU DMA read */
    layout.dram_bandwidth_legacy_3copy_mbps = (4.0 * frame_bytes * fps) / 1000000.0;

    /* Zero-copy pipeline: (1) CSI DMA write to shared DMA-BUF -> (2) GPU DMA read for TensorRT */
    layout.dram_bandwidth_zerocopy_mbps = (2.0 * frame_bytes * fps) / 1000000.0;

    layout.dram_bandwidth_savings_mbps = layout.dram_bandwidth_legacy_3copy_mbps - layout.dram_bandwidth_zerocopy_mbps;
    layout.dram_bandwidth_savings_percent = 50.0;

    return layout;
}

DmaBufRingPool::DmaBufRingPool(size_t slot_count, const DmaPlaneLayout &layout)
    : layout_(layout) {
    size_t count = std::clamp(slot_count, MIN_DMA_SLOTS, MAX_DMA_SLOTS);
    slots_.reserve(count);

    for (size_t i = 0; i < count; i++) {
        auto slot = std::make_unique<DmaBufSlot>();
        slot->slot_idx = static_cast<uint32_t>(i);
        slot->dmabuf_fd = static_cast<int>(100 + i); /* Simulated DMA-BUF file descriptor */
        slot->state = DmaSlotState::SLOT_FREE;
        slot->sequence_number = 0;
        slot->capture_timestamp_ns = 0;
        slot->fence_syncpoint_id = 0;
        slot->fence_signaled = false;
        slot->buffer_size = layout_.total_aligned_size_bytes;
        slot->buffer_memory.resize(slot->buffer_size, 0);

        slots_.push_back(std::move(slot));
    }
}

DmaBufRingPool::~DmaBufRingPool() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &slot : slots_) {
        if (!slot->buffer_memory.empty()) {
            secure_zero_dma_buffer(slot->buffer_memory.data(), slot->buffer_memory.size());
            slot->buffer_memory.clear();
        }
        slot->state = DmaSlotState::SLOT_FREE;
    }
}

DmaErrorCode DmaBufRingPool::v4l2_qbuf(uint32_t slot_idx) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (slot_idx >= slots_.size()) {
        return DmaErrorCode::DMA_ERR_INVALID_SLOT;
    }

    auto &slot = slots_[slot_idx];

    /* CRITICAL SAFETY CHECK (CWE-362): Never allow camera DMA write into a buffer currently in GPU inference */
    if (slot->state == DmaSlotState::SLOT_TRT_GPU_INFER) {
        race_violations_prevented_++;
        return DmaErrorCode::DMA_ERR_WRITE_DURING_INFER;
    }

    slot->state = DmaSlotState::SLOT_V4L2_DMA_CAPTURE;
    slot->fence_signaled = false;
    slot->fence_syncpoint_id = 0;
    return DmaErrorCode::DMA_OK;
}

DmaErrorCode DmaBufRingPool::v4l2_dqbuf(uint32_t slot_idx, uint64_t seq_num, uint64_t timestamp_ns, uint64_t fence_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (slot_idx >= slots_.size()) {
        return DmaErrorCode::DMA_ERR_INVALID_SLOT;
    }

    auto &slot = slots_[slot_idx];

    /* Check monotonic sequence */
    if (last_sequence_ != 0 && seq_num <= last_sequence_) {
        sequence_faults_detected_++;
        return DmaErrorCode::DMA_ERR_SEQUENCE_DISCONTINUITY;
    }

    last_sequence_ = seq_num;
    total_frames_processed_++;

    slot->sequence_number = seq_num;
    slot->capture_timestamp_ns = timestamp_ns;
    slot->fence_syncpoint_id = fence_id;
    slot->fence_signaled = false; /* Fence must be signaled by CSI/VIC before TRT read */
    slot->state = DmaSlotState::SLOT_DMA_READY_FENCED;

    return DmaErrorCode::DMA_OK;
}

DmaErrorCode DmaBufRingPool::signal_hardware_fence(uint32_t slot_idx, uint64_t fence_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (slot_idx >= slots_.size()) {
        return DmaErrorCode::DMA_ERR_INVALID_SLOT;
    }

    auto &slot = slots_[slot_idx];
    if (slot->fence_syncpoint_id == fence_id) {
        slot->fence_signaled = true;
        return DmaErrorCode::DMA_OK;
    }

    return DmaErrorCode::DMA_ERR_FENCE_NOT_SIGNALED;
}

DmaErrorCode DmaBufRingPool::trt_acquire_buffer(uint32_t slot_idx) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (slot_idx >= slots_.size()) {
        return DmaErrorCode::DMA_ERR_INVALID_SLOT;
    }

    auto &slot = slots_[slot_idx];

    /* Must be in SLOT_DMA_READY_FENCED state */
    if (slot->state != DmaSlotState::SLOT_DMA_READY_FENCED) {
        return DmaErrorCode::DMA_ERR_INVALID_SLOT;
    }

    /* Fence must be signaled */
    if (!slot->fence_signaled) {
        return DmaErrorCode::DMA_ERR_FENCE_NOT_SIGNALED;
    }

    slot->state = DmaSlotState::SLOT_TRT_GPU_INFER;
    return DmaErrorCode::DMA_OK;
}

DmaErrorCode DmaBufRingPool::trt_release_buffer(uint32_t slot_idx) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (slot_idx >= slots_.size()) {
        return DmaErrorCode::DMA_ERR_INVALID_SLOT;
    }

    auto &slot = slots_[slot_idx];
    slot->state = DmaSlotState::SLOT_FREE;
    slot->fence_signaled = false;
    return DmaErrorCode::DMA_OK;
}

DmaSlotState DmaBufRingPool::get_slot_state(uint32_t slot_idx) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (slot_idx >= slots_.size()) {
        return DmaSlotState::SLOT_FREE;
    }
    return slots_[slot_idx]->state.load();
}

const DmaBufSlot &DmaBufRingPool::get_slot(uint32_t slot_idx) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return *slots_.at(slot_idx);
}

DmaBufSlot &DmaBufRingPool::get_slot_mut(uint32_t slot_idx) {
    std::lock_guard<std::mutex> lock(mutex_);
    return *slots_.at(slot_idx);
}

} // namespace logihertz
