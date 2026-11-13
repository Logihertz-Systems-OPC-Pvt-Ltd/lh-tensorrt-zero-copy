# Security Policy

## Supported Versions

Logihertz Systems provides security maintenance and vulnerability updates for the following release branches of `lh-tensorrt-zero-copy`:

| Version | Supported          |
|---------|--------------------|
| 0.1.x   | :white_check_mark: |
| < 0.1.0 | :x:                |

## Edge Vision DMA & Zero-Copy Threat Model

`lh-tensorrt-zero-copy` governs critical high-bandwidth memory boundaries between V4L2 camera capture hardware, kernel DMA subsystems, unified system memory (`DMA-BUF`), and NVIDIA TensorRT GPU inference engines on autonomous edge AI robotics platforms (NVIDIA Jetson AGX Orin, Orin NX, Xavier):

1. **Hardware IOMMU / ARM SMMU Stage-1 Isolation (CWE-119)**:
   - Direct Memory Access (DMA) engines operating without active IOMMU/SMMU translation can read or corrupt arbitrary physical system memory.
   - Enforcing stage-1 SMMU isolation ensures camera CSI/VIP DMA units and GPU memory spaces remain strictly bounded to granted `DMA-BUF` allocations.
2. **Asynchronous Race Conditions & Write-While-Infer Tearing (CWE-362)**:
   - Concurrent writing by camera DMA into a shared buffer while TensorRT kernels execute results in torn, corrupted image frames and unpredictable inference results.
   - A deterministic syncpoint fence state machine (`SLOT_FREE -> SLOT_V4L2_DMA_CAPTURE -> SLOT_DMA_READY_FENCED -> SLOT_TRT_GPU_INFER -> SLOT_FREE`) strictly prevents enqueuing active inference buffers for capture.
3. **Insecure TensorRT Engine Deserialization & Untrusted Plugins (CWE-502)**:
   - Loading unverified `.engine` / `.plan` artifacts or unvetted custom plugin shared libraries can lead to arbitrary code execution in the inference daemon.
   - Mandatory cryptographic signature checks on engine files and disabling untrusted plugin dynamic loading.
4. **Surface Pitch Alignment & Buffer Out-of-Bounds (CWE-125)**:
   - Mismatches between hardware capture pitch (256-byte Tegra alignment) and GPU tensor pitch cause memory access out-of-bounds.
   - Strict 256-byte pitch and 4096-byte SMMU page geometry calculation eliminates buffer overflow risks.
5. **Memory Remanence & DMA-BUF Page Leaks (CWE-226)**:
   - Residual video frames in shared memory across application restarts or crashes can expose sensitive surveillance imagery.
   - Secure volatile zeroization of all DMA buffers on pool teardown and enforcing restrictive `0600` permissions on `/dev/dma_heap`.

## Reporting a Vulnerability

If you discover a security flaw, DMA race condition, or memory corruption vulnerability in `lh-tensorrt-zero-copy`:

1. **Do not disclose publicly** through issue trackers or public forums.
2. Email the maintainers directly at **`ops@logihertz.com`** and **`security@logihertz.com`** with the subject line:
   `[SECURITY] Vulnerability in lh-tensorrt-zero-copy: <Brief Description>`
3. Include in your report:
   - Affected component (DMA pool allocator, syncpoint fence engine, tensor bridge, or posture auditor).
   - Target SoC platform (e.g., Jetson Orin AGX, Tegra234, Linux 5.15 / 6.6 LTS).
   - Minimal proof-of-concept reproducing the race condition, memory leak, or alignment fault.
4. The Logihertz security team will acknowledge receipt within 48 hours and provide a coordinated disclosure timeline.
