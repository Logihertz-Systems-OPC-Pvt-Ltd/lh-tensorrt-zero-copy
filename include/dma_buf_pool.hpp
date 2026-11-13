#ifndef LH_DMA_BUF_POOL_HPP
#define LH_DMA_BUF_POOL_HPP

#include "zerocopy_types.hpp"
#include <vector>
#include <memory>
#include <mutex>

namespace logihertz {

void secure_zero_dma_buffer(volatile void *ptr, size_t size);

DmaPlaneLayout calculate_dma_surface_layout(
    uint32_t width,
    uint32_t height,
    PixelFormat format,
    double framerate_fps = 60.0,
    size_t pitch_align = TEGRA_PITCH_ALIGNMENT,
    size_t page_align = SMMU_PAGE_ALIGNMENT
);

class DmaBufRingPool {
public:
    DmaBufRingPool(size_t slot_count, const DmaPlaneLayout &layout);
    ~DmaBufRingPool();

    /* Disable copying */
    DmaBufRingPool(const DmaBufRingPool &) = delete;
    DmaBufRingPool &operator=(const DmaBufRingPool &) = delete;

    /* V4L2 Camera Capture Side */
    DmaErrorCode v4l2_qbuf(uint32_t slot_idx);
    DmaErrorCode v4l2_dqbuf(uint32_t slot_idx, uint64_t seq_num, uint64_t timestamp_ns, uint64_t fence_id);

    /* Hardware Syncpoint Fence Signaling */
    DmaErrorCode signal_hardware_fence(uint32_t slot_idx, uint64_t fence_id);

    /* TensorRT GPU Inference Side */
    DmaErrorCode trt_acquire_buffer(uint32_t slot_idx);
    DmaErrorCode trt_release_buffer(uint32_t slot_idx);

    /* State & Telemetry */
    DmaSlotState get_slot_state(uint32_t slot_idx) const;
    size_t get_slot_count() const { return slots_.size(); }
    const DmaBufSlot &get_slot(uint32_t slot_idx) const;
    DmaBufSlot &get_slot_mut(uint32_t slot_idx);
    const DmaPlaneLayout &get_layout() const { return layout_; }

    uint64_t get_last_sequence() const { return last_sequence_; }
    uint64_t get_total_frames_processed() const { return total_frames_processed_; }
    uint64_t get_race_violations_prevented() const { return race_violations_prevented_; }
    uint64_t get_sequence_faults_detected() const { return sequence_faults_detected_; }

private:
    DmaPlaneLayout layout_;
    std::vector<std::unique_ptr<DmaBufSlot>> slots_;
    mutable std::mutex mutex_;
    uint64_t last_sequence_{0};
    uint64_t total_frames_processed_{0};
    uint64_t race_violations_prevented_{0};
    uint64_t sequence_faults_detected_{0};
};

} // namespace logihertz

#endif // LH_DMA_BUF_POOL_HPP
