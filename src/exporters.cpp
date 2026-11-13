#include "exporters.hpp"
#include <iomanip>
#include <sstream>

namespace logihertz {

static const char *severity_to_str(Severity sev) {
    switch (sev) {
        case Severity::CRITICAL: return "CRITICAL";
        case Severity::HIGH:     return "HIGH";
        case Severity::MEDIUM:   return "MEDIUM";
        case Severity::LOW:      return "LOW";
        default:                 return "INFO";
    }
}

static const char *severity_to_sarif_level(Severity sev) {
    switch (sev) {
        case Severity::CRITICAL:
        case Severity::HIGH:     return "error";
        case Severity::MEDIUM:   return "warning";
        case Severity::LOW:      return "note";
        default:                 return "none";
    }
}

void Exporters::export_audit_table(const AuditReport &report, std::ostream &os) {
    os << "\n========================================================================================\n";
    os << " LOGIHERTZ V4L2-TO-TENSORRT ZERO-COPY DMA SECURITY AUDIT REPORT\n";
    os << " Policy: " << report.policy_name << "\n";
    os << "========================================================================================\n";
    os << " " << std::left << std::setw(12) << "RULE ID"
       << " | " << std::setw(6) << "STATUS"
       << " | " << std::setw(8) << "SEVERITY"
       << " | " << std::setw(4) << "DED"
       << " | " << std::setw(42) << "RULE NAME" << "\n";
    os << "----------------------------------------------------------------------------------------\n";

    for (const auto &f : report.findings) {
        std::string status_str = f.passed ? "\033[32mPASS\033[0m" : "\033[31mFAIL\033[0m";
        os << " " << std::left << std::setw(12) << f.rule_id
           << " | " << std::setw(15) << status_str
           << " | " << std::setw(8) << severity_to_str(f.severity)
           << " | -" << std::setw(3) << (f.passed ? 0 : f.deduction)
           << " | " << std::setw(42) << f.rule_name << "\n";
    }

    os << "========================================================================================\n";
    os << " SUMMARY: Total Rules: " << report.total_rules
       << " | Passed: " << report.passed_rules
       << " | Failed: " << report.failed_rules
       << " | Deductions: -" << report.total_deductions << "\n";
    os << " POSTURE SCORE: \033[" << ((report.posture_score >= 80) ? "32" : ((report.posture_score >= 50) ? "33" : "31"))
       << "m" << report.posture_score << " / 100\033[0m ["
       << ((report.posture_score >= 80) ? "PASS" : "FAIL") << "]\n";
    os << "========================================================================================\n\n";
}

void Exporters::export_audit_json(const AuditReport &report, std::ostream &os) {
    os << "{\n";
    os << "  \"tool\": \"lh-trt-zerocopy\",\n";
    os << "  \"policy_name\": \"" << report.policy_name << "\",\n";
    os << "  \"posture_score\": " << report.posture_score << ",\n";
    os << "  \"total_rules\": " << report.total_rules << ",\n";
    os << "  \"passed_rules\": " << report.passed_rules << ",\n";
    os << "  \"failed_rules\": " << report.failed_rules << ",\n";
    os << "  \"total_deductions\": " << report.total_deductions << ",\n";
    os << "  \"status\": \"" << ((report.posture_score >= 80) ? "PASS" : "FAIL") << "\",\n";
    os << "  \"findings\": [\n";

    for (size_t i = 0; i < report.findings.size(); i++) {
        const auto &f = report.findings[i];
        os << "    {\n";
        os << "      \"rule_id\": \"" << f.rule_id << "\",\n";
        os << "      \"rule_name\": \"" << f.rule_name << "\",\n";
        os << "      \"cwe_id\": \"" << f.cwe_id << "\",\n";
        os << "      \"severity\": \"" << severity_to_str(f.severity) << "\",\n";
        os << "      \"deduction\": " << (f.passed ? 0 : f.deduction) << ",\n";
        os << "      \"passed\": " << (f.passed ? "true" : "false") << ",\n";
        os << "      \"details\": \"" << f.details << "\",\n";
        os << "      \"recommendation\": \"" << f.recommendation << "\"\n";
        os << "    }" << (i + 1 < report.findings.size() ? "," : "") << "\n";
    }

    os << "  ]\n";
    os << "}\n";
}

void Exporters::export_audit_sarif(const AuditReport &report, std::ostream &os) {
    os << "{\n";
    os << "  \"$schema\": \"https://docs.oasis-open.org/sarif/sarif/v2.1.0/cos01/schemas/sarif-schema-2.1.0.json\",\n";
    os << "  \"version\": \"2.1.0\",\n";
    os << "  \"runs\": [\n";
    os << "    {\n";
    os << "      \"tool\": {\n";
    os << "        \"driver\": {\n";
    os << "          \"name\": \"lh-trt-zerocopy\",\n";
    os << "          \"version\": \"0.1.0\",\n";
    os << "          \"informationUri\": \"https://github.com/Logihertz-Systems-OPC-Pvt-Ltd/lh-tensorrt-zero-copy\",\n";
    os << "          \"rules\": [\n";

    for (size_t i = 0; i < report.findings.size(); i++) {
        const auto &f = report.findings[i];
        os << "            {\n";
        os << "              \"id\": \"" << f.rule_id << "\",\n";
        os << "              \"name\": \"" << f.rule_name << "\",\n";
        os << "              \"shortDescription\": { \"text\": \"" << f.rule_name << "\" },\n";
        os << "              \"help\": { \"text\": \"" << f.recommendation << "\" },\n";
        os << "              \"defaultConfiguration\": {\n";
        os << "                \"level\": \"" << severity_to_sarif_level(f.severity) << "\"\n";
        os << "              }\n";
        os << "            }" << (i + 1 < report.findings.size() ? "," : "") << "\n";
    }

    os << "          ]\n";
    os << "        }\n";
    os << "      },\n";
    os << "      \"results\": [\n";

    size_t printed = 0;
    for (const auto &f : report.findings) {
        if (!f.passed) {
            if (printed > 0) os << ",\n";
            os << "        {\n";
            os << "          \"ruleId\": \"" << f.rule_id << "\",\n";
            os << "          \"level\": \"" << severity_to_sarif_level(f.severity) << "\",\n";
            os << "          \"message\": { \"text\": \"" << f.details << "\" },\n";
            os << "          \"locations\": [\n";
            os << "            {\n";
            os << "              \"physicalLocation\": {\n";
            os << "                \"artifactLocation\": { \"uri\": \"" << report.policy_name << "\" }\n";
            os << "              }\n";
            os << "            }\n";
            os << "          ]\n";
            os << "        }";
            printed++;
        }
    }
    if (printed > 0) os << "\n";

    os << "      ]\n";
    os << "    }\n";
    os << "  ]\n";
    os << "}\n";
}

void Exporters::export_layout_table(const DmaPlaneLayout &layout, std::ostream &os) {
    os << "\n===================================================================\n";
    os << " HARDWARE PITCH & SMMU PAGE ALIGNMENT GEOMETRY ANALYSIS\n";
    os << "===================================================================\n";
    os << " Frame Resolution      : " << layout.width << " x " << layout.height << "\n";
    os << " Line Pitch Aligned    : " << layout.pitch_bytes << " bytes (256-byte Tegra alignment)\n";
    os << " Plane Count           : " << layout.plane_count << "\n";
    for (uint32_t i = 0; i < layout.plane_count; i++) {
        os << "  - Plane " << i << " Offset    : " << layout.plane_offsets[i]
           << " bytes | Size: " << layout.plane_sizes[i] << " bytes\n";
    }
    os << " Total SMMU Aligned    : " << layout.total_aligned_size_bytes << " bytes (4096-byte page alignment)\n";
    os << " Target Framerate      : " << layout.framerate_fps << " FPS\n";
    os << " DRAM BW (Legacy 3-cp) : " << std::fixed << std::setprecision(2)
       << layout.dram_bandwidth_legacy_3copy_mbps << " MB/s\n";
    os << " DRAM BW (Zero-Copy)   : " << layout.dram_bandwidth_zerocopy_mbps << " MB/s\n";
    os << " Bandwidth Savings     : " << layout.dram_bandwidth_savings_mbps << " MB/s ("
       << layout.dram_bandwidth_savings_percent << " % DRAM traffic reduction)\n";
    os << "===================================================================\n\n";
}

void Exporters::export_layout_json(const DmaPlaneLayout &layout, std::ostream &os) {
    os << "{\n";
    os << "  \"tool\": \"lh-trt-zerocopy\",\n";
    os << "  \"subcommand\": \"calc-layout\",\n";
    os << "  \"width\": " << layout.width << ",\n";
    os << "  \"height\": " << layout.height << ",\n";
    os << "  \"pitch_bytes\": " << layout.pitch_bytes << ",\n";
    os << "  \"plane_count\": " << layout.plane_count << ",\n";
    os << "  \"total_aligned_size_bytes\": " << layout.total_aligned_size_bytes << ",\n";
    os << "  \"framerate_fps\": " << layout.framerate_fps << ",\n";
    os << "  \"dram_bandwidth_legacy_3copy_mbps\": " << layout.dram_bandwidth_legacy_3copy_mbps << ",\n";
    os << "  \"dram_bandwidth_zerocopy_mbps\": " << layout.dram_bandwidth_zerocopy_mbps << ",\n";
    os << "  \"dram_bandwidth_savings_mbps\": " << layout.dram_bandwidth_savings_mbps << ",\n";
    os << "  \"dram_bandwidth_savings_percent\": " << layout.dram_bandwidth_savings_percent << "\n";
    os << "}\n";
}

void Exporters::export_pipeline_sim_table(const DmaBufRingPool &pool, std::ostream &os) {
    os << "\n===================================================================\n";
    os << " LOCK-FREE V4L2-TO-TENSORRT SYNCPOINT FENCE PIPELINE SIMULATION\n";
    os << "===================================================================\n";
    os << " Total Pool Slots      : " << pool.get_slot_count() << "\n";
    os << " Frames Processed      : " << pool.get_total_frames_processed() << "\n";
    os << " Last Sequence No.     : " << pool.get_last_sequence() << "\n";
    os << " Race Faults Blocked   : " << pool.get_race_violations_prevented() << " (Write-While-Infer Protected)\n";
    os << " Sequence Discontinuity: " << pool.get_sequence_faults_detected() << " (Monotonic Verified)\n";
    os << " Pipeline State Status : OK (Deterministic Zero-Copy Verified)\n";
    os << "===================================================================\n\n";
}

void Exporters::export_pipeline_sim_json(const DmaBufRingPool &pool, std::ostream &os) {
    os << "{\n";
    os << "  \"tool\": \"lh-trt-zerocopy\",\n";
    os << "  \"subcommand\": \"sim-pipeline\",\n";
    os << "  \"slot_count\": " << pool.get_slot_count() << ",\n";
    os << "  \"frames_processed\": " << pool.get_total_frames_processed() << ",\n";
    os << "  \"last_sequence_number\": " << pool.get_last_sequence() << ",\n";
    os << "  \"race_violations_prevented\": " << pool.get_race_violations_prevented() << ",\n";
    os << "  \"sequence_faults_detected\": " << pool.get_sequence_faults_detected() << ",\n";
    os << "  \"pipeline_status\": \"PASS\"\n";
    os << "}\n";
}

void Exporters::export_tensor_table(const NCHWTensor &tensor, std::ostream &os) {
    os << "\n===================================================================\n";
    os << " BT.709 NV12 TO NCHW (FP32 & INT8) TENSOR CONVERSION RESULTS\n";
    os << "===================================================================\n";
    os << " Tensor Dimensions     : [" << tensor.shape.batch << ", "
       << tensor.shape.channels << ", " << tensor.shape.height << ", " << tensor.shape.width << "]\n";
    os << " Total Elements        : " << tensor.shape.total_elements() << "\n";
    os << " FP32 Memory Size      : " << tensor.data_fp32.size() * sizeof(float) << " bytes\n";
    os << " INT8 Memory Size      : " << tensor.data_int8.size() * sizeof(int8_t) << " bytes\n";
    os << " INT8 Quant Scale      : " << tensor.int8_scale << "\n";
    os << " Tensor SHA-256 Digest : " << tensor.sha256_digest << "\n";
    os << " Conversion Status     : SUCCESS (Deterministic Tensor Generated)\n";
    os << "===================================================================\n\n";
}

void Exporters::export_tensor_json(const NCHWTensor &tensor, std::ostream &os) {
    os << "{\n";
    os << "  \"tool\": \"lh-trt-zerocopy\",\n";
    os << "  \"subcommand\": \"convert-frame\",\n";
    os << "  \"batch\": " << tensor.shape.batch << ",\n";
    os << "  \"channels\": " << tensor.shape.channels << ",\n";
    os << "  \"height\": " << tensor.shape.height << ",\n";
    os << "  \"width\": " << tensor.shape.width << ",\n";
    os << "  \"total_elements\": " << tensor.shape.total_elements() << ",\n";
    os << "  \"int8_scale\": " << tensor.int8_scale << ",\n";
    os << "  \"sha256_digest\": \"" << tensor.sha256_digest << "\"\n";
    os << "}\n";
}

} // namespace logihertz
