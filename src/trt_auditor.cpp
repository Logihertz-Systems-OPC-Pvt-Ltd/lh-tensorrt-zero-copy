#include "trt_auditor.hpp"
#include <algorithm>

namespace logihertz {

AuditReport TrtAuditor::evaluate_posture(const PolicyConfig &policy) {
    AuditReport report;
    report.policy_name = policy.policy_name;
    report.total_rules = MAX_AUDIT_RULES;
    report.passed_rules = 0;
    report.failed_rules = 0;
    report.total_deductions = 0;

    /* Rule 1: LH-TRT-001 (CRITICAL, 25, CWE-119) */
    {
        AuditFinding f;
        f.rule_id = "LH-TRT-001";
        f.rule_name = "Hardware IOMMU / ARM SMMU Stage-1 DMA Isolation";
        f.cwe_id = "CWE-119";
        f.severity = Severity::CRITICAL;
        f.deduction = 25;
        if (policy.iommu_smmu_isolation_enabled && !policy.allow_unrestricted_physical_dma) {
            f.passed = true;
            f.details = "ARM SMMU stage-1 translation active; physical DMA restricted to mapped DMA-BUF buffers.";
            f.recommendation = "Compliant with hardware IOMMU isolation guidelines.";
            report.passed_rules++;
        } else {
            f.passed = false;
            f.details = "SMMU isolation disabled or unrestricted physical DMA access permitted.";
            f.recommendation = "Enable ARM SMMU stage-1 translation and disable raw physical memory DMA.";
            report.total_deductions += f.deduction;
            report.failed_rules++;
        }
        report.findings.push_back(f);
    }

    /* Rule 2: LH-TRT-002 (CRITICAL, 25, CWE-362) */
    {
        AuditFinding f;
        f.rule_id = "LH-TRT-002";
        f.rule_name = "Hardware Syncpoint Fence & Write-While-Infer Race Block";
        f.cwe_id = "CWE-362";
        f.severity = Severity::CRITICAL;
        f.deduction = 25;
        if (policy.hardware_sync_fence_enforced && !policy.allow_concurrent_dma_write_during_infer) {
            f.passed = true;
            f.details = "Hardware syncpoint fences enforced between capture and inference; write-during-infer tearing blocked.";
            f.recommendation = "Compliant with real-time zero-copy synchronization safety.";
            report.passed_rules++;
        } else {
            f.passed = false;
            f.details = "Syncpoint fences disabled or concurrent camera DMA write allowed while TensorRT reads buffer.";
            f.recommendation = "Enforce hardware syncpoint barriers and reject V4L2 queue requests on active inference slots.";
            report.total_deductions += f.deduction;
            report.failed_rules++;
        }
        report.findings.push_back(f);
    }

    /* Rule 3: LH-TRT-003 (CRITICAL, 25, CWE-502) */
    {
        AuditFinding f;
        f.rule_id = "LH-TRT-003";
        f.rule_name = "Cryptographic Engine Verification & Untrusted Plugin Block";
        f.cwe_id = "CWE-502";
        f.severity = Severity::CRITICAL;
        f.deduction = 25;
        if (policy.verify_trt_engine_signature && !policy.allow_untrusted_trt_plugins) {
            f.passed = true;
            f.details = "TensorRT .engine artifacts cryptographically verified and untrusted shared library plugins blocked.";
            f.recommendation = "Compliant with secure model deserialization standards.";
            report.passed_rules++;
        } else {
            f.passed = false;
            f.details = "Unverified engine deserialization permitted or untrusted plugin shared libraries allowed.";
            f.recommendation = "Require cryptographic signatures for all .engine plans and prohibit unverified plugins.";
            report.total_deductions += f.deduction;
            report.failed_rules++;
        }
        report.findings.push_back(f);
    }

    /* Rule 4: LH-TRT-004 (HIGH, 10, CWE-400) */
    {
        AuditFinding f;
        f.rule_id = "LH-TRT-004";
        f.rule_name = "DMA-BUF File Descriptor FD_CLOEXEC & Safe Pool Sizing";
        f.cwe_id = "CWE-400";
        f.severity = Severity::HIGH;
        f.deduction = 10;
        if (policy.dmabuf_fd_cloexec_enforced && policy.dmabuf_pool_slots >= 3 && policy.dmabuf_pool_slots <= 16) {
            f.passed = true;
            f.details = "DMA-BUF file descriptors enforce FD_CLOEXEC and pool size (" + std::to_string(policy.dmabuf_pool_slots) + ") is safely bounded [3..16].";
            f.recommendation = "Compliant with file descriptor lifecycle and starvation limits.";
            report.passed_rules++;
        } else {
            f.passed = false;
            f.details = "FD_CLOEXEC missing on DMA-BUF descriptors or pool slots (" + std::to_string(policy.dmabuf_pool_slots) + ") out of safe bounds [3..16].";
            f.recommendation = "Set O_CLOEXEC on all DMA-BUF exports and configure between 3 and 16 ring slots.";
            report.total_deductions += f.deduction;
            report.failed_rules++;
        }
        report.findings.push_back(f);
    }

    /* Rule 5: LH-TRT-005 (HIGH, 10, CWE-125) */
    {
        AuditFinding f;
        f.rule_id = "LH-TRT-005";
        f.rule_name = "Hardware Pitch (256B) & SMMU Page (4KB) Geometry Bounds";
        f.cwe_id = "CWE-125";
        f.severity = Severity::HIGH;
        f.deduction = 10;
        if (policy.pitch_alignment_bytes >= 256 && policy.page_alignment_bytes >= 4096 && policy.strict_plane_bounds_check) {
            f.passed = true;
            f.details = "Surface pitch aligned to >= 256B, page aligned to >= 4096B, and strict plane bounds checks enforced.";
            f.recommendation = "Compliant with hardware surface geometry invariants.";
            report.passed_rules++;
        } else {
            f.passed = false;
            f.details = "Unaligned surface pitch (" + std::to_string(policy.pitch_alignment_bytes) + "B < 256B), page (" +
                        std::to_string(policy.page_alignment_bytes) + "B < 4096B), or plane bounds check disabled.";
            f.recommendation = "Align line pitch to 256-byte multiples and total allocations to 4KB page boundaries.";
            report.total_deductions += f.deduction;
            report.failed_rules++;
        }
        report.findings.push_back(f);
    }

    /* Rule 6: LH-TRT-006 (HIGH, 10, CWE-400) */
    {
        AuditFinding f;
        f.rule_id = "LH-TRT-006";
        f.rule_name = "True Zero-Copy DMA-BUF Enforcement & CPU memcpy Block";
        f.cwe_id = "CWE-400";
        f.severity = Severity::HIGH;
        f.deduction = 10;
        if (policy.zero_copy_dmabuf_enabled && !policy.allow_cpu_memcpy_fallback) {
            f.passed = true;
            f.details = "Zero-copy DMA-BUF unified memory path enforced; CPU memcpy staging fallbacks disabled.";
            f.recommendation = "Compliant with deterministic zero-copy bandwidth requirements.";
            report.passed_rules++;
        } else {
            f.passed = false;
            f.details = "Zero-copy disabled or CPU memcpy staging fallback active (inducing up to 50% extra DRAM traffic).";
            f.recommendation = "Enforce V4L2_MEMORY_DMABUF and cudaImportExternalMemory; disable CPU fallback copies.";
            report.total_deductions += f.deduction;
            report.failed_rules++;
        }
        report.findings.push_back(f);
    }

    /* Rule 7: LH-TRT-007 (HIGH, 10, CWE-226) */
    {
        AuditFinding f;
        f.rule_id = "LH-TRT-007";
        f.rule_name = "DMA-BUF Zeroization on Free & Restricted Heap Permissions";
        f.cwe_id = "CWE-226";
        f.severity = Severity::HIGH;
        f.deduction = 10;
        if (policy.zeroize_dmabuf_on_free && (policy.dma_heap_permissions_octal == 0600 || policy.dma_heap_permissions_octal == 600)) {
            f.passed = true;
            f.details = "DMA-BUF pages sanitized on teardown and /dev/dma_heap permissions restricted to 0600.";
            f.recommendation = "Compliant with memory remanence and heap access control standards.";
            report.passed_rules++;
        } else {
            f.passed = false;
            f.details = "Buffers not sanitized on free or /dev/dma_heap permissions too permissive (expected 0600).";
            f.recommendation = "Perform secure zeroization of all shared video frames on release and restrict heap permissions to 0600.";
            report.total_deductions += f.deduction;
            report.failed_rules++;
        }
        report.findings.push_back(f);
    }

    /* Rule 8: LH-TRT-008 (MEDIUM, 4, CWE-662) */
    {
        AuditFinding f;
        f.rule_id = "LH-TRT-008";
        f.rule_name = "MIPI CSI Frame-Timeout Watchdog & Monotonic Sequence Check";
        f.cwe_id = "CWE-662";
        f.severity = Severity::MEDIUM;
        f.deduction = 4;
        if (policy.csi_frame_timeout_ms <= 100 && policy.verify_monotonic_v4l2_sequence) {
            f.passed = true;
            f.details = "Camera frame timeout watchdog (" + std::to_string(policy.csi_frame_timeout_ms) + " ms <= 100 ms) and monotonic sequence checks active.";
            f.recommendation = "Compliant with sensor stream integrity and stall detection.";
            report.passed_rules++;
        } else {
            f.passed = false;
            f.details = "Excessive frame timeout (" + std::to_string(policy.csi_frame_timeout_ms) + " ms > 100 ms) or sequence monotonicity unverified.";
            f.recommendation = "Configure a camera watchdog timeout <= 100 ms and reject non-monotonic V4L2 sequence numbers.";
            report.total_deductions += f.deduction;
            report.failed_rules++;
        }
        report.findings.push_back(f);
    }

    /* Rule 9: LH-TRT-009 (MEDIUM, 4, CWE-400) */
    {
        AuditFinding f;
        f.rule_id = "LH-TRT-009";
        f.rule_name = "Preallocated Execution Contexts & Memory Locking (mlock)";
        f.cwe_id = "CWE-400";
        f.severity = Severity::MEDIUM;
        f.deduction = 4;
        if (policy.preallocate_trt_execution_context && policy.mlock_dma_pool_buffers) {
            f.passed = true;
            f.details = "TensorRT execution context preallocated and DMA pool metadata locked into RAM (mlock).";
            f.recommendation = "Compliant with deterministic real-time memory management.";
            report.passed_rules++;
        } else {
            f.passed = false;
            f.details = "Dynamic GPU allocation during infer loop or DMA buffers eligible for swap paging.";
            f.recommendation = "Preallocate all TensorRT execution contexts and lock DMA pool memory via mlock().";
            report.total_deductions += f.deduction;
            report.failed_rules++;
        }
        report.findings.push_back(f);
    }

    /* Rule 10: LH-TRT-010 (LOW, 1, CWE-778) */
    {
        AuditFinding f;
        f.rule_id = "LH-TRT-010";
        f.rule_name = "End-to-End Latency Telemetry & Jetson Clock Lock Guard";
        f.cwe_id = "CWE-778";
        f.severity = Severity::LOW;
        f.deduction = 1;
        if (policy.log_e2e_latency_telemetry && policy.verify_jetson_clocks_locked) {
            f.passed = true;
            f.details = "Nanosecond capture-to-infer latency telemetry active and Jetson GPU/EMC clocks verified locked.";
            f.recommendation = "Compliant with deterministic latency observability.";
            report.passed_rules++;
        } else {
            f.passed = false;
            f.details = "Latency telemetry disabled or Jetson hardware clocks dynamic/unlocked (causing jitter).";
            f.recommendation = "Log capture-to-inference timestamps and lock EMC/GPU clocks with jetson_clocks.";
            report.total_deductions += f.deduction;
            report.failed_rules++;
        }
        report.findings.push_back(f);
    }

    if (report.total_deductions >= 100) {
        report.posture_score = 0;
    } else {
        report.posture_score = 100 - report.total_deductions;
    }

    return report;
}

} // namespace logihertz
