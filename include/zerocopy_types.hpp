#ifndef LH_ZEROCOPY_TYPES_HPP
#define LH_ZEROCOPY_TYPES_HPP

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <atomic>

namespace logihertz {

constexpr size_t TEGRA_PITCH_ALIGNMENT = 256;      /* 256-byte Tegra VIC / NVCSI pitch alignment */
constexpr size_t SMMU_PAGE_ALIGNMENT = 4096;        /* 4096-byte ARM SMMU / IOMMU page alignment */
constexpr size_t MAX_DMA_SLOTS = 16;
constexpr size_t MIN_DMA_SLOTS = 3;
constexpr size_t MAX_AUDIT_RULES = 10;

enum class PixelFormat {
    NV12,
    UYVY,
    RGBA8,
    PLANAR_RGB_FP32,
    PLANAR_RGB_INT8
};

enum class DmaSlotState {
    SLOT_FREE = 0,
    SLOT_V4L2_DMA_CAPTURE = 1,
    SLOT_DMA_READY_FENCED = 2,
    SLOT_TRT_GPU_INFER = 3
};

enum class DmaErrorCode {
    DMA_OK = 0,
    DMA_ERR_NO_FREE_SLOTS = 1,
    DMA_ERR_WRITE_DURING_INFER = 2,        /* CWE-362 Race: Write while GPU inferencing */
    DMA_ERR_FENCE_NOT_SIGNALED = 3,        /* Attempted infer before hardware syncpoint signaled */
    DMA_ERR_SEQUENCE_DISCONTINUITY = 4,    /* Non-monotonic V4L2 frame sequence number */
    DMA_ERR_INVALID_SLOT = 5,
    DMA_ERR_UNALIGNED_GEOMETRY = 6
};

enum class Severity {
    INFO = 0,
    LOW = 1,
    MEDIUM = 4,
    HIGH = 10,
    CRITICAL = 25
};

struct DmaPlaneLayout {
    uint32_t width{0};
    uint32_t height{0};
    PixelFormat format{PixelFormat::NV12};
    uint32_t pitch_bytes{0};
    uint32_t plane_count{0};
    uint32_t plane_offsets[4]{0, 0, 0, 0};
    uint32_t plane_sizes[4]{0, 0, 0, 0};
    size_t total_aligned_size_bytes{0};
    double framerate_fps{60.0};
    double dram_bandwidth_zerocopy_mbps{0.0};
    double dram_bandwidth_legacy_3copy_mbps{0.0};
    double dram_bandwidth_savings_mbps{0.0};
    double dram_bandwidth_savings_percent{50.0};
};

struct DmaBufSlot {
    uint32_t slot_idx{0};
    int dmabuf_fd{-1};
    std::atomic<DmaSlotState> state{DmaSlotState::SLOT_FREE};
    uint64_t sequence_number{0};
    uint64_t capture_timestamp_ns{0};
    uint64_t fence_syncpoint_id{0};
    bool fence_signaled{false};
    std::vector<uint8_t> buffer_memory;
    size_t buffer_size{0};
};

struct TensorShape {
    int batch{1};
    int channels{3};
    int height{0};
    int width{0};
    size_t total_elements() const {
        return static_cast<size_t>(batch * channels * height * width);
    }
};

struct NCHWTensor {
    TensorShape shape;
    std::vector<float> data_fp32;
    std::vector<int8_t> data_int8;
    float int8_scale{1.0f / 127.0f};
    std::string sha256_digest;
};

struct PolicyConfig {
    std::string policy_name{"default_zerocopy_policy"};
    bool iommu_smmu_isolation_enabled{true};
    bool allow_unrestricted_physical_dma{false};
    bool hardware_sync_fence_enforced{true};
    bool allow_concurrent_dma_write_during_infer{false};
    bool verify_trt_engine_signature{true};
    bool allow_untrusted_trt_plugins{false};
    bool dmabuf_fd_cloexec_enforced{true};
    uint32_t dmabuf_pool_slots{4};
    uint32_t pitch_alignment_bytes{256};
    uint32_t page_alignment_bytes{4096};
    bool strict_plane_bounds_check{true};
    bool zero_copy_dmabuf_enabled{true};
    bool allow_cpu_memcpy_fallback{false};
    bool zeroize_dmabuf_on_free{true};
    uint32_t dma_heap_permissions_octal{0600};
    uint32_t csi_frame_timeout_ms{50};
    bool verify_monotonic_v4l2_sequence{true};
    bool preallocate_trt_execution_context{true};
    bool mlock_dma_pool_buffers{true};
    bool log_e2e_latency_telemetry{true};
    bool verify_jetson_clocks_locked{true};
};

struct AuditFinding {
    std::string rule_id;
    std::string rule_name;
    std::string cwe_id;
    Severity severity{Severity::INFO};
    uint32_t deduction{0};
    bool passed{true};
    std::string details;
    std::string recommendation;
};

struct AuditReport {
    std::string policy_name;
    uint32_t total_rules{0};
    uint32_t passed_rules{0};
    uint32_t failed_rules{0};
    uint32_t total_deductions{0};
    uint32_t posture_score{100};
    std::vector<AuditFinding> findings;
};

} // namespace logihertz

#endif // LH_ZEROCOPY_TYPES_HPP
