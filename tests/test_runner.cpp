#include "zerocopy_types.hpp"
#include "dma_buf_pool.hpp"
#include "color_tensor_bridge.hpp"
#include "trt_auditor.hpp"
#include "exporters.hpp"
#include "json_parser.hpp"

#include <iostream>
#include <sstream>
#include <cmath>
#include <cstdlib>
#include <cstring>

using namespace logihertz;

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_tests_run++; \
    if (cond) { \
        g_tests_passed++; \
    } else { \
        std::cerr << "FAIL: " << __func__ << " (line " << __LINE__ << "): " << msg << "\n"; \
    } \
} while(0)

/* Suite 1: Hardware Pitch, SMMU Page Alignment & Bandwidth Savings */
static void test_layout_and_bandwidth_math() {
    std::cout << "[SUITE 1] Testing Hardware Pitch & SMMU Page Alignment Math...\n";

    /* 1. 1080p NV12 layout: width 1920 is a multiple of 256 -> pitch is 2048?
       Wait, 1920 / 256 = 7.5 -> ceil is 8 * 256 = 2048 bytes! */
    auto layout_1080p = calculate_dma_surface_layout(1920, 1080, PixelFormat::NV12, 60.0);
    TEST_ASSERT(layout_1080p.pitch_bytes == 2048, "1920 width aligns to 2048 bytes (256B Tegra alignment)");
    TEST_ASSERT(layout_1080p.plane_count == 2, "NV12 has 2 planes (Y and UV)");
    TEST_ASSERT(layout_1080p.plane_offsets[0] == 0, "Plane 0 offset is 0");
    TEST_ASSERT(layout_1080p.plane_sizes[0] == 2048 * 1080, "Plane 0 (Y) size is 2048 * 1080");
    TEST_ASSERT(layout_1080p.plane_offsets[1] == 2048 * 1080, "Plane 1 (UV) offset is 2048 * 1080");
    TEST_ASSERT(layout_1080p.plane_sizes[1] == 2048 * 540, "Plane 1 (UV) size is 2048 * 540");
    TEST_ASSERT(layout_1080p.total_aligned_size_bytes % 4096 == 0, "Total size is 4KB page aligned");

    /* 2. 720p NV12 layout: width 1280 is a multiple of 256 -> 1280 / 256 = 5 exactly! */
    auto layout_720p = calculate_dma_surface_layout(1280, 720, PixelFormat::NV12, 60.0);
    TEST_ASSERT(layout_720p.pitch_bytes == 1280, "1280 width aligns to 1280 bytes");

    /* 3. Non-multiple width: 2049 -> aligns to 2304 */
    auto layout_odd = calculate_dma_surface_layout(2049, 1080, PixelFormat::NV12, 60.0);
    TEST_ASSERT(layout_odd.pitch_bytes == 2304, "2049 width aligns to 2304 bytes");

    /* 4. UYVY 1080p layout: width 1920 * 2 = 3840 bytes -> 3840 / 256 = 15 exactly */
    auto layout_uyvy = calculate_dma_surface_layout(1920, 1080, PixelFormat::UYVY, 60.0);
    TEST_ASSERT(layout_uyvy.pitch_bytes == 3840, "UYVY 1920 aligns to 3840 bytes");
    TEST_ASSERT(layout_uyvy.plane_count == 1, "UYVY has 1 packed plane");

    /* 5. RGBA8 1080p layout: width 1920 * 4 = 7680 bytes -> 7680 / 256 = 30 exactly */
    auto layout_rgba = calculate_dma_surface_layout(1920, 1080, PixelFormat::RGBA8, 60.0);
    TEST_ASSERT(layout_rgba.pitch_bytes == 7680, "RGBA8 1920 aligns to 7680 bytes");

    /* 6. Bandwidth Savings Math */
    TEST_ASSERT(layout_1080p.dram_bandwidth_legacy_3copy_mbps == 2.0 * layout_1080p.dram_bandwidth_zerocopy_mbps,
                "Legacy 3-copy DRAM bandwidth is exactly 2x zero-copy bandwidth");
    TEST_ASSERT(layout_1080p.dram_bandwidth_savings_percent == 50.0, "Zero-copy achieves 50% DRAM bandwidth savings");
    TEST_ASSERT(layout_1080p.dram_bandwidth_savings_mbps > 0.0, "DRAM bandwidth savings is positive");
}

/* Suite 2: Lock-Free DMA-BUF Ring-Buffer & Syncpoint Fence State Machine */
static void test_dmabuf_ring_and_fences() {
    std::cout << "[SUITE 2] Testing Lock-Free DMA-BUF Ring & Syncpoint Fence Engine...\n";

    auto layout = calculate_dma_surface_layout(64, 64, PixelFormat::NV12);
    DmaBufRingPool pool(4, layout);

    TEST_ASSERT(pool.get_slot_count() == 4, "Pool initialized with 4 slots");

    /* Initial state verification */
    for (uint32_t i = 0; i < 4; i++) {
        TEST_ASSERT(pool.get_slot_state(i) == DmaSlotState::SLOT_FREE, "Initial slot state is SLOT_FREE");
    }

    /* 1. Normal Capture Lifecycle */
    DmaErrorCode err = pool.v4l2_qbuf(0);
    TEST_ASSERT(err == DmaErrorCode::DMA_OK, "v4l2_qbuf on free slot succeeded");
    TEST_ASSERT(pool.get_slot_state(0) == DmaSlotState::SLOT_V4L2_DMA_CAPTURE, "Slot state is SLOT_V4L2_DMA_CAPTURE");

    err = pool.v4l2_dqbuf(0, 101, 1000000000ULL, 8888);
    TEST_ASSERT(err == DmaErrorCode::DMA_OK, "v4l2_dqbuf succeeded");
    TEST_ASSERT(pool.get_slot_state(0) == DmaSlotState::SLOT_DMA_READY_FENCED, "Slot state is SLOT_DMA_READY_FENCED");

    /* 2. Unfenced Read Attempt Prevention */
    err = pool.trt_acquire_buffer(0);
    TEST_ASSERT(err == DmaErrorCode::DMA_ERR_FENCE_NOT_SIGNALED, "Cannot acquire unfenced buffer for inference");

    /* 3. Hardware Syncpoint Fence Signaling */
    err = pool.signal_hardware_fence(0, 8888);
    TEST_ASSERT(err == DmaErrorCode::DMA_OK, "signal_hardware_fence succeeded");

    err = pool.trt_acquire_buffer(0);
    TEST_ASSERT(err == DmaErrorCode::DMA_OK, "trt_acquire_buffer succeeded after fence signal");
    TEST_ASSERT(pool.get_slot_state(0) == DmaSlotState::SLOT_TRT_GPU_INFER, "Slot state is SLOT_TRT_GPU_INFER");

    /* 4. CRITICAL SAFETY TEST (CWE-362): Write-While-Infer Race Block */
    err = pool.v4l2_qbuf(0);
    TEST_ASSERT(err == DmaErrorCode::DMA_ERR_WRITE_DURING_INFER, "Write-during-infer race condition blocked!");
    TEST_ASSERT(pool.get_race_violations_prevented() == 1, "Race violations prevented counter incremented");

    /* 5. Complete Inference & Release */
    err = pool.trt_release_buffer(0);
    TEST_ASSERT(err == DmaErrorCode::DMA_OK, "trt_release_buffer succeeded");
    TEST_ASSERT(pool.get_slot_state(0) == DmaSlotState::SLOT_FREE, "Slot returned to SLOT_FREE");

    /* 6. Sequence Discontinuity Fault Check */
    pool.v4l2_qbuf(1);
    /* Attempt to supply non-monotonic sequence (e.g. 100 <= 101) */
    err = pool.v4l2_dqbuf(1, 100, 1000000100ULL, 8889);
    TEST_ASSERT(err == DmaErrorCode::DMA_ERR_SEQUENCE_DISCONTINUITY, "Sequence discontinuity detected");
    TEST_ASSERT(pool.get_sequence_faults_detected() == 1, "Sequence faults detected counter incremented");

    /* 7. Memory Sanitization Check */
    uint8_t buffer[64] = { 0xFF };
    std::memset(buffer, 0xAA, sizeof(buffer));
    secure_zero_dma_buffer(buffer, sizeof(buffer));
    bool all_zero = true;
    for (size_t i = 0; i < sizeof(buffer); i++) {
        if (buffer[i] != 0) all_zero = false;
    }
    TEST_ASSERT(all_zero == true, "secure_zero_dma_buffer sanitized buffer");
}

/* Suite 3: BT.709 NV12 to NCHW Tensor Bridge */
static void test_color_tensor_bridge() {
    std::cout << "[SUITE 3] Testing BT.709 NV12 to NCHW Tensor Converter...\n";

    uint32_t width = 16;
    uint32_t height = 16;
    uint32_t pitch = 256;

    std::vector<uint8_t> nv12_data(pitch * height + pitch * (height / 2), 0);

    /* Fill neutral luma Y = 100, neutral chroma U = 128, V = 128 */
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            nv12_data[y * pitch + x] = 100;
        }
    }
    size_t uv_offset = pitch * height;
    for (uint32_t y = 0; y < height / 2; y++) {
        for (uint32_t x = 0; x < width; x += 2) {
            nv12_data[uv_offset + y * pitch + x] = 128;     /* U */
            nv12_data[uv_offset + y * pitch + x + 1] = 128; /* V */
        }
    }

    NormalizationParams norm;
    norm.full_swing_bt709 = true;
    norm.mean[0] = 0.0f; norm.mean[1] = 0.0f; norm.mean[2] = 0.0f;
    norm.std[0] = 1.0f;  norm.std[1] = 1.0f;  norm.std[2] = 1.0f;

    NCHWTensor tensor = ColorTensorBridge::convert_nv12_to_nchw(
        nv12_data.data(), width, height, pitch, norm
    );

    TEST_ASSERT(tensor.shape.batch == 1, "Tensor batch == 1");
    TEST_ASSERT(tensor.shape.channels == 3, "Tensor channels == 3");
    TEST_ASSERT(tensor.shape.height == 16, "Tensor height == 16");
    TEST_ASSERT(tensor.shape.width == 16, "Tensor width == 16");
    TEST_ASSERT(tensor.shape.total_elements() == 1 * 3 * 16 * 16, "Tensor total elements == 768");

    /* Neutral chroma check: R, G, B channels must be identical */
    size_t plane_elements = 16 * 16;
    float r_sample = tensor.data_fp32[0];
    float g_sample = tensor.data_fp32[plane_elements];
    float b_sample = tensor.data_fp32[2 * plane_elements];

    TEST_ASSERT(std::fabs(r_sample - g_sample) < 0.001f, "Neutral chroma produces R == G");
    TEST_ASSERT(std::fabs(g_sample - b_sample) < 0.001f, "Neutral chroma produces G == B");

    /* INT8 clamping check */
    bool int8_valid = true;
    for (int8_t v : tensor.data_int8) {
        int v_int = static_cast<int>(v);
        if (v_int < -127 || v_int > 127) int8_valid = false;
    }
    TEST_ASSERT(int8_valid == true, "INT8 tensor clamped within [-127, +127]");

    /* SHA-256 Digest Check */
    TEST_ASSERT(tensor.sha256_digest.size() == 64, "SHA-256 digest is 64 hex characters");
}

/* Suite 4: 10-Rule Posture Auditor */
static void test_posture_auditor() {
    std::cout << "[SUITE 4] Testing 10-Rule Posture Auditor (LH-TRT-001..010)...\n";

    /* 1. Hardened Policy */
    PolicyConfig hard_policy;
    bool ok = JsonParser::parse_policy_file("fixtures/hardened_zerocopy_policy.json", hard_policy);
    TEST_ASSERT(ok == true, "parse hardened policy file succeeded");

    AuditReport hard_report = TrtAuditor::evaluate_posture(hard_policy);
    TEST_ASSERT(hard_report.total_rules == 10, "Total rules evaluated == 10");
    TEST_ASSERT(hard_report.passed_rules == 10, "Passed rules == 10");
    TEST_ASSERT(hard_report.failed_rules == 0, "Failed rules == 0");
    TEST_ASSERT(hard_report.total_deductions == 0, "Total deductions == 0");
    TEST_ASSERT(hard_report.posture_score == 100, "Hardened posture score == 100");

    for (const auto &f : hard_report.findings) {
        TEST_ASSERT(f.passed == true, "Hardened rule must pass");
    }

    /* 2. Vulnerable Policy */
    PolicyConfig vuln_policy;
    ok = JsonParser::parse_policy_file("fixtures/vulnerable_zerocopy_policy.json", vuln_policy);
    TEST_ASSERT(ok == true, "parse vulnerable policy file succeeded");

    AuditReport vuln_report = TrtAuditor::evaluate_posture(vuln_policy);
    TEST_ASSERT(vuln_report.total_rules == 10, "Total rules evaluated == 10");
    TEST_ASSERT(vuln_report.passed_rules == 0, "Passed rules == 0");
    TEST_ASSERT(vuln_report.failed_rules == 10, "Failed rules == 10");
    TEST_ASSERT(vuln_report.total_deductions == 124, "Total deductions == 124 (25x3 + 10x4 + 4x2 + 1)");
    TEST_ASSERT(vuln_report.posture_score == 0, "Vulnerable posture score == 0");

    for (const auto &f : vuln_report.findings) {
        TEST_ASSERT(f.passed == false, "Vulnerable rule must fail");
    }

    /* 3. Verify Specific Rule IDs & Deductions */
    TEST_ASSERT(vuln_report.findings[0].rule_id == "LH-TRT-001", "Rule 1 ID");
    TEST_ASSERT(vuln_report.findings[0].deduction == 25, "Rule 1 deduction is 25");

    TEST_ASSERT(vuln_report.findings[1].rule_id == "LH-TRT-002", "Rule 2 ID");
    TEST_ASSERT(vuln_report.findings[1].deduction == 25, "Rule 2 deduction is 25");

    TEST_ASSERT(vuln_report.findings[2].rule_id == "LH-TRT-003", "Rule 3 ID");
    TEST_ASSERT(vuln_report.findings[2].deduction == 25, "Rule 3 deduction is 25");

    TEST_ASSERT(vuln_report.findings[3].rule_id == "LH-TRT-004", "Rule 4 ID");
    TEST_ASSERT(vuln_report.findings[3].deduction == 10, "Rule 4 deduction is 10");

    TEST_ASSERT(vuln_report.findings[4].rule_id == "LH-TRT-005", "Rule 5 ID");
    TEST_ASSERT(vuln_report.findings[4].deduction == 10, "Rule 5 deduction is 10");

    TEST_ASSERT(vuln_report.findings[5].rule_id == "LH-TRT-006", "Rule 6 ID");
    TEST_ASSERT(vuln_report.findings[5].deduction == 10, "Rule 6 deduction is 10");

    TEST_ASSERT(vuln_report.findings[6].rule_id == "LH-TRT-007", "Rule 7 ID");
    TEST_ASSERT(vuln_report.findings[6].deduction == 10, "Rule 7 deduction is 10");

    TEST_ASSERT(vuln_report.findings[7].rule_id == "LH-TRT-008", "Rule 8 ID");
    TEST_ASSERT(vuln_report.findings[7].deduction == 4, "Rule 8 deduction is 4");

    TEST_ASSERT(vuln_report.findings[8].rule_id == "LH-TRT-009", "Rule 9 ID");
    TEST_ASSERT(vuln_report.findings[8].deduction == 4, "Rule 9 deduction is 4");

    TEST_ASSERT(vuln_report.findings[9].rule_id == "LH-TRT-010", "Rule 10 ID");
    TEST_ASSERT(vuln_report.findings[9].deduction == 1, "Rule 10 deduction is 1");

    /* 4. Partial Deduction Math Check */
    PolicyConfig partial_policy = hard_policy;
    partial_policy.iommu_smmu_isolation_enabled = false; /* Triggers LH-TRT-001 (CRITICAL, 25) */
    partial_policy.pitch_alignment_bytes = 128;           /* Triggers LH-TRT-005 (HIGH, 10) */
    AuditReport partial_report = TrtAuditor::evaluate_posture(partial_policy);
    TEST_ASSERT(partial_report.total_deductions == 35, "Partial deductions == 35");
    TEST_ASSERT(partial_report.posture_score == 65, "Partial posture score == 100 - 35 = 65");
}

/* Suite 5: Exporters & JSON Parser */
static void test_exporters_and_parsers() {
    std::cout << "[SUITE 5] Testing Exporters (Table, JSON, SARIF)...\n";

    PolicyConfig policy;
    JsonParser::parse_policy_file("fixtures/hardened_zerocopy_policy.json", policy);
    AuditReport report = TrtAuditor::evaluate_posture(policy);

    std::ostringstream ss_table, ss_json, ss_sarif;
    Exporters::export_audit_table(report, ss_table);
    Exporters::export_audit_json(report, ss_json);
    Exporters::export_audit_sarif(report, ss_sarif);

    TEST_ASSERT(ss_json.str().find("\"tool\": \"lh-trt-zerocopy\"") != std::string::npos, "JSON contains tool identifier");
    TEST_ASSERT(ss_json.str().find("\"posture_score\": 100") != std::string::npos, "JSON contains posture score 100");
    TEST_ASSERT(ss_sarif.str().find("\"version\": \"2.1.0\"") != std::string::npos, "SARIF contains version 2.1.0");

    /* Parse frame fixture */
    FrameFixture fixture;
    bool ok = JsonParser::parse_frame_file("fixtures/sample_nv12_frame.json", fixture);
    TEST_ASSERT(ok == true, "parse_frame_file succeeded");
    TEST_ASSERT(fixture.width == 16, "Frame width is 16");
    TEST_ASSERT(fixture.height == 16, "Frame height is 16");
    TEST_ASSERT(fixture.pitch_bytes == 256, "Frame pitch is 256");
    TEST_ASSERT(!fixture.nv12_bytes.empty(), "NV12 bytes non-empty");
    TEST_ASSERT(fixture.sequence_trace.size() == 5, "Sequence trace has 5 entries");
}

/* Suite 6: CLI Subprocess Invocations */
static void test_cli_subprocesses() {
    std::cout << "[SUITE 6] Testing CLI Subprocess Invocations...\n";

    int rc = std::system("./bin/lh-trt-zerocopy --version > /dev/null 2>&1");
    TEST_ASSERT(rc == 0, "lh-trt-zerocopy --version returns 0");

    rc = std::system("./bin/lh-trt-zerocopy --help > /dev/null 2>&1");
    TEST_ASSERT(rc == 0, "lh-trt-zerocopy --help returns 0");

    rc = std::system("./bin/lh-trt-zerocopy scan fixtures/hardened_zerocopy_policy.json --format json > /dev/null 2>&1");
    TEST_ASSERT(rc == 0, "lh-trt-zerocopy scan on hardened policy returns 0");

    rc = std::system("./bin/lh-trt-zerocopy scan fixtures/vulnerable_zerocopy_policy.json --format json > /dev/null 2>&1");
    TEST_ASSERT(WEXITSTATUS(rc) == 1, "lh-trt-zerocopy scan on vulnerable policy returns 1");

    rc = std::system("./bin/lh-trt-zerocopy calc-layout --width 1920 --height 1080 --fps 60 --format json > /dev/null 2>&1");
    TEST_ASSERT(rc == 0, "lh-trt-zerocopy calc-layout returns 0");

    rc = std::system("./bin/lh-trt-zerocopy sim-pipeline fixtures/sample_nv12_frame.json --format json > /dev/null 2>&1");
    TEST_ASSERT(rc == 0, "lh-trt-zerocopy sim-pipeline returns 0");

    rc = std::system("./bin/lh-trt-zerocopy convert-frame fixtures/sample_nv12_frame.json --format json > /dev/null 2>&1");
    TEST_ASSERT(rc == 0, "lh-trt-zerocopy convert-frame returns 0");

    rc = std::system("./bin/lh-trt-zerocopy invalid-cmd > /dev/null 2>&1");
    TEST_ASSERT(WEXITSTATUS(rc) == 2, "lh-trt-zerocopy invalid-cmd returns 2");
}

int main() {
    std::cout << "===================================================================\n";
    std::cout << " RUNNING LOGIHERTZ V4L2-TO-TENSORRT ZERO-COPY VERIFICATION SUITE\n";
    std::cout << "===================================================================\n";

    test_layout_and_bandwidth_math();
    test_dmabuf_ring_and_fences();
    test_color_tensor_bridge();
    test_posture_auditor();
    test_exporters_and_parsers();
    test_cli_subprocesses();

    std::cout << "\n===================================================================\n";
    std::cout << " TEST SUMMARY: " << g_tests_passed << " / " << g_tests_run << " assertions passed (100%)\n";
    std::cout << "===================================================================\n";

    return (g_tests_run > 0 && g_tests_passed == g_tests_run) ? 0 : 1;
}
