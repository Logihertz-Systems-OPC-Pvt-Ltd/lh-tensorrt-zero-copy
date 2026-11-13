# lh-tensorrt-zero-copy

**Production-Grade, Zero-Dependency C++17 V4L2-to-TensorRT Zero-Copy DMA Allocator, Hardware Pitch & SMMU Page Alignment Engine, Lock-Free Capture-to-Inference Fence State Machine, BT.709 YUV-to-NCHW Tensor Bridge, and 10-Rule Edge Vision DMA Security Posture Auditor.**

[![CI](https://github.com/Logihertz-Systems-OPC-Pvt-Ltd/lh-tensorrt-zero-copy/actions/workflows/ci.yml/badge.svg)](https://github.com/Logihertz-Systems-OPC-Pvt-Ltd/lh-tensorrt-zero-copy/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![Language](https://img.shields.io/badge/Language-ISO%20C%2B%2B17-brightgreen.svg)](https://en.cppreference.com/w/cpp/17)
[![Zero External Dependencies](https://img.shields.io/badge/Dependencies-0%20(Pure%20C%2B%2B17)-success.svg)](Makefile)

---

## 1. Overview & Architectural Rationale

Edge AI embedded systems (such as NVIDIA Jetson Orin, Xavier, and industrial ARM SoCs) operating high-throughput computer vision pipelines (1080p60, 4K30, multi-camera surround sensing) face severe memory bandwidth bottlenecks when image frames traverse traditional user-space copy boundaries. 

In a naive camera-to-inference pipeline:
1. Camera sensor ISP writes to V4L2 kernel ring buffers via DMA.
2. User-space application executes `mmap()` and `memcpy()` to pull frames from kernel space to CPU RAM.
3. Preprocessing converts color spaces (NV12 to RGB) and normalizes pixel values via secondary CPU RAM buffers.
4. Host-to-Device `cudaMemcpy()` transfers pixel buffers across system memory to GPU global memory for inference.

This legacy approach performs **three distinct round-trip memory copy operations**, saturating DRAM controllers, inducing CPU cache thrashing, elevating thermal dissipation, and injecting unconstrained frame jitter.

`lh-tensorrt-zero-copy` implements an industrial-grade, zero-dependency C++17 unified zero-copy shared memory architecture:
- Allocates contiguous, SMMU-isolated hardware DMA buffers directly via Linux `/dev/dma_heap` or V4L2 `DMA-BUF` file descriptors.
- Enforces strict hardware pitch alignment (256-byte multiples for NVIDIA Tegra hardware surface engines) and ARM SMMU stage-1 page alignment (4096 bytes).
- Implements a lock-free, fence-synchronized DMA buffer ring pool enforcing safe state transitions:
  $$\text{FREE} \longrightarrow \text{V4L2\_DMA\_CAPTURE} \longrightarrow \text{DMA\_READY\_FENCED} \longrightarrow \text{TRT\_GPU\_INFER} \longrightarrow \text{FREE}$$
- Directly imports `DMA-BUF` file descriptors into CUDA / TensorRT memory spaces via `cudaImportExternalMemory()` with zero user-space CPU staging copies.
- Integrates a hardware-accurate ITU-R BT.709 NV12 semi-planar to planar NCHW tensor conversion bridge with symmetric INT8 quantization.
- Audits system configuration against the **10-Rule Edge Vision DMA Security Posture Matrix** (`LH-TRT-001` through `LH-TRT-010`), providing defense against DMA race conditions (CWE-362), buffer overflows (CWE-119), untrusted plugin execution (CWE-502), and memory remanence (CWE-226).

---

## 2. Zero-Copy Hardware Architecture

```
+-----------------------------------------------------------------------------------+
|                        MIPI CSI-2 Camera Sensor / ISP                             |
+-----------------------------------------------------------------------------------+
                                         │
                                         ▼ (Direct Hardware DMA)
+-----------------------------------------------------------------------------------+
|               Linux DMA-BUF Unified Memory Subsystem (/dev/dma_heap)               |
|  - ARM SMMU Stage-1 Page Isolated (4096-Byte Page Alignment)                      |
|  - Hardware Surface Pitch Aligned (256-Byte Chunk Boundary)                       |
|  - File Descriptors created with O_CLOEXEC & Perms 0600                           |
|                                                                                   |
|   +---------------------------------------------------------------------------+   |
|   |                      Lock-Free Ring Buffer Pool                           |   |
|   |  Slot 0: [FREE]                                                           |   |
|   |  Slot 1: [V4L2_DMA_CAPTURE]  <-- Camera ISP DMA filling buffer            |   |
|   |  Slot 2: [DMA_READY_FENCED]  <-- Syncpoint fence signaled                 |   |
|   |  Slot 3: [TRT_GPU_INFER]     <-- TensorRT CUDA Core executing kernel      |   |
|   +---------------------------------------------------------------------------+   |
+-----------------------------------------------------------------------------------+
             │                                                           ▲
             │ (Kernel Syncpoint Fence Signaled)                         │
             ▼                                                           │
+------------------------------------------------------------------------┼----------+
|                  NVIDIA TensorRT GPU Inference Engine                  │          |
|  - cudaImportExternalMemory(dmabuf_fd)                                 │          |
|  - Lock-Free Fence Wait (Zero CPU Polling)                             │          |
|  - Fused BT.709 Color Conversion & Normalization (NCHW Tensor)         │          |
|  - Symmetric INT8 Tensor Quantization: [-127, +127]                    │          |
|  - Secure Zeroization on Deallocation                                  │          |
+------------------------------------------------------------------------┼----------+
                                                                         │
                                                 Release DMA Buffer ─────┘
```

---

## 3. Mathematical Foundations & Invariants

### 3.1 Hardware Surface Pitch Alignment
NVIDIA Tegra memory controllers and VIC (Video Image Compositor) hardware surface engines mandate that the stride (line pitch in bytes) of every 2D image plane must be an integer multiple of 256 bytes ($A_{\text{pitch}} = 256$):

$$P = \left\lceil \frac{W \cdot b_{\text{elem}}}{A_{\text{pitch}}} \right\rceil \cdot A_{\text{pitch}} = \left\lceil \frac{W \cdot b_{\text{elem}}}{256} \right\rceil \cdot 256$$

Where:
- $W$: Frame width in pixels.
- $b_{\text{elem}}$: Bytes per element (1 for NV12 Y and UV planes, 2 for UYVY, 4 for RGBA).
- For a standard 1080p frame ($W = 1920$):
  $$P = \left\lceil \frac{1920 \cdot 1}{256} \right\rceil \cdot 256 = \lceil 7.5 \rceil \cdot 256 = 8 \cdot 256 = 2048 \text{ bytes}$$

### 3.2 SMMU Stage-1 Page Allocation
The total allocated memory per buffer must satisfy the ARM SMMU page boundary invariant ($A_{\text{page}} = 4096$):

$$S_{\text{raw}} = \sum_{i=0}^{N_{\text{planes}}-1} P_i \cdot H_i$$

$$S_{\text{dma}} = \left\lceil \frac{S_{\text{raw}}}{A_{\text{page}}} \right\rceil \cdot A_{\text{page}} = \left\lceil \frac{S_{\text{raw}}}{4096} \right\rceil \cdot 4096$$

For $1920 \times 1080$ NV12:
- $S_Y = 2048 \times 1080 = 2,211,840 \text{ bytes}$
- $S_{UV} = 2048 \times 540 = 1,105,920 \text{ bytes}$
- $S_{\text{raw}} = 2,211,840 + 1,105,920 = 3,317,760 \text{ bytes}$
- $S_{\text{raw}} \pmod{4096} = 0 \implies 810 \text{ exact pages} \implies S_{\text{dma}} = 3,317,760 \text{ bytes}$.

### 3.3 DRAM Bandwidth Reduction Model
For a frame of size $S_{\text{dma}}$ bytes running at $f$ frames per second:
- **Legacy 3-Copy Pipeline**:
  $$BW_{\text{legacy}} = BW_{\text{isp}\to\text{dram}} + BW_{\text{dram}\to\text{cpu}} + BW_{\text{cpu}\to\text{dram}} + BW_{\text{dram}\to\text{gpu}} = 4 \cdot S_{\text{dma}} \cdot f$$
- **True Zero-Copy Pipeline**:
  $$BW_{\text{zerocopy}} = BW_{\text{isp}\to\text{dram}} + BW_{\text{dram}\to\text{gpu}} = 2 \cdot S_{\text{dma}} \cdot f$$
- **Bandwidth Reduction Ratio**:
  $$\Delta BW = \frac{BW_{\text{legacy}} - BW_{\text{zerocopy}}}{BW_{\text{legacy}}} = \frac{4 - 2}{4} = 50.00\%$$
- At 1080p60 ($S_{\text{dma}} = 3.318 \text{ MB}$, $f = 60 \text{ Hz}$):
  $$BW_{\text{legacy}} = 4 \cdot 3.317760 \cdot 60 = 796.26 \text{ MB/s}$$
  $$BW_{\text{zerocopy}} = 2 \cdot 3.317760 \cdot 60 = 398.13 \text{ MB/s}$$
  $$\text{DRAM Bandwidth Saved} = 398.13 \text{ MB/s}$$

### 3.4 ITU-R BT.709 Color Matrix & Normalization
Conversion from semi-planar NV12 ($Y, C_b, C_r$) to normalized planar RGB ($[1, 3, H, W]$):

For Full Swing:
$$\begin{bmatrix} R \\ G \\ B \end{bmatrix} = \begin{bmatrix} 1.0 & 0.0 & 1.5748 \\ 1.0 & -0.1873 & -0.4681 \\ 1.0 & 1.8556 & 0.0 \end{bmatrix} \begin{bmatrix} Y \\ U - 128 \\ V - 128 \end{bmatrix}$$

Standard FP32 Normalization:
$$X_{c, y, x} = \frac{R_{y, x} / 255.0 - \mu_c}{\sigma_c}$$

Calibrated Symmetric INT8 Quantization ($[-127, +127]$):
$$q = \text{clamp}\left( \left\lfloor \frac{X}{s_{\text{int8}}} + 0.5 \right\rfloor, -127, 127 \right)$$

---

## 4. 10-Rule Edge Vision DMA Security Posture Matrix

| Rule ID | Severity | Deduction | CWE | Rule Name & Enforcement Rationale |
|:---|:---:|:---:|:---:|:---|
| **LH-TRT-001** | CRITICAL | -25 | CWE-119 | **Hardware IOMMU / ARM SMMU Stage-1 DMA Isolation**: Enforces physical address isolation between camera ISP and GPU, prohibiting arbitrary bus-master physical DMA exploitation. |
| **LH-TRT-002** | CRITICAL | -25 | CWE-362 | **Hardware Syncpoint Fence & Write-While-Infer Race Block**: Enforces hardware syncpoint fences and blocks queueing camera capture buffers while actively locked by TensorRT inference (prevents frame tearing). |
| **LH-TRT-003** | CRITICAL | -25 | CWE-502 | **Cryptographic Engine Verification & Untrusted Plugin Block**: Prohibits loading unsigned serialized TensorRT `.plan`/`.engine` files and blocks untrusted dynamic plugin shared libraries (`.so`). |
| **LH-TRT-004** | HIGH | -10 | CWE-400 | **DMA-BUF File Descriptor `FD_CLOEXEC` & Safe Pool Sizing**: Enforces close-on-exec flag on all exported DMA-BUF file descriptors and restricts pool sizing within safe bounds $[3 \dots 16]$ to prevent resource starvation. |
| **LH-TRT-005** | HIGH | -10 | CWE-125 | **Hardware Pitch (256B) & SMMU Page (4KB) Geometry Bounds**: Validates line pitch $\ge 256\text{B}$ and total surface buffer $\ge 4096\text{B}$ to prevent out-of-bounds DMA access faults. |
| **LH-TRT-006** | HIGH | -10 | CWE-400 | **True Zero-Copy DMA-BUF Enforcement & CPU `memcpy` Block**: Mandates native DMA-BUF unified memory paths, strictly forbidding CPU `memcpy` staging fallbacks that incur 50% extra DRAM traffic. |
| **LH-TRT-007** | HIGH | -10 | CWE-226 | **DMA-BUF Zeroization on Free & Restricted Heap Permissions**: Sanitizes volatile shared DMA memory on release to prevent remanence leakage and enforces `/dev/dma_heap` permissions `0600`. |
| **LH-TRT-008** | MEDIUM | -4 | CWE-662 | **MIPI CSI Frame-Timeout Watchdog & Monotonic Sequence Check**: Detects CSI desynchronization and packet drops with hardware watchdogs $\le 100\text{ ms}$ and monotonic sequence verification. |
| **LH-TRT-009** | MEDIUM | -4 | CWE-400 | **Preallocated Execution Contexts & Memory Locking (`mlock`)**: Preallocates TensorRT scratch memory and locks DMA pool metadata pages in RAM via `mlock()` to eliminate dynamic paging latency spikes. |
| **LH-TRT-010** | LOW | -1 | CWE-778 | **End-to-End Latency Telemetry & Jetson Clock Lock Guard**: Emits high-precision nanosecond capture-to-inference latency timestamps and verifies EMC/GPU clocks are pinned via `jetson_clocks`. |

**Scoring Model**:
$$S_{\text{posture}} = \max\left(0, 100 - \sum \text{Deductions}\right)$$
- `PASS` condition: $S_{\text{posture}} = 100$ and 0 failed critical rules (Exit Code 0).
- `FAIL` condition: $S_{\text{posture}} < 100$ or any failed rule (Exit Code 1).

---

## 5. Build & Verification

### 5.1 Compilation
Requires any ISO C++17 compliant compiler (`g++` or `clang++`) and standard Make:

```bash
# Clean, compile, and run full verification suite
make clean all test

# Build with Clang++
make CXX=clang++ clean all test
```

### 5.2 Verification Test Suite
The built-in test suite (`tests/test_runner`) executes 6 comprehensive test suites covering 115 test assertions:
- **Suite 1**: Hardware Pitch & SMMU Page Alignment Math (1080p, 720p, UYVY, RGBA8, and 50% DRAM bandwidth model).
- **Suite 2**: Lock-Free DMA-BUF Ring & Syncpoint Fence Engine (slot state transitions, fence signaling, `DMA_ERR_WRITE_DURING_INFER` race prevention, sequence discontinuity fault detection, volatile buffer sanitization).
- **Suite 3**: ITU-R BT.709 NV12 to NCHW Tensor Bridge (neutral chroma channel equivalence, FP32 normalization, INT8 $[-127, +127]$ clamping, FIPS 180-4 SHA-256 tensor digest).
- **Suite 4**: 10-Rule Posture Auditor (evaluation of hardened policy, vulnerable policy, and partial deduction arithmetic).
- **Suite 5**: Exporters & Serialization (Table, Canonical JSON, OASIS SARIF v2.1.0).
- **Suite 6**: Subprocess CLI Invocations (argument parsing, return codes, help/version strings).

---

## 6. CLI Usage & Command Reference

### 6.1 Audit Security Posture (`scan`)
Audits a zero-copy configuration policy JSON against the 10-rule posture matrix:

```bash
# Scan hardened policy with ANSI table output
./bin/lh-trt-zerocopy scan fixtures/hardened_zerocopy_policy.json --format table

# Scan with canonical JSON output
./bin/lh-trt-zerocopy scan fixtures/hardened_zerocopy_policy.json --format json

# Export OASIS SARIF v2.1.0 for GitHub Security tab integration
./bin/lh-trt-zerocopy scan fixtures/hardened_zerocopy_policy.json --format sarif --output report.sarif
```

### 6.2 Calculate Hardware Surface Layout (`calc-layout`)
Computes 256-byte pitch, 4KB page bounds, and DRAM bandwidth reduction metrics:

```bash
# Calculate 1080p60 NV12 layout
./bin/lh-trt-zerocopy calc-layout --width 1920 --height 1080 --fps 60

# Calculate 4K30 UYVY layout with JSON export
./bin/lh-trt-zerocopy calc-layout --width 3840 --height 2160 --pixel-format UYVY --fps 30 --format json
```

### 6.3 Simulate Lock-Free DMA Pipeline (`sim-pipeline`)
Simulates the hardware capture-to-inference ring pool across a frame trace, verifying race condition blocking and sequence continuity:

```bash
./bin/lh-trt-zerocopy sim-pipeline fixtures/sample_nv12_frame.json --slots 4
```

### 6.4 Convert NV12 Frame to NCHW Tensor (`convert-frame`)
Converts semi-planar NV12 camera frames into planar NCHW FP32/INT8 tensors with cryptographic SHA-256 integrity verification:

```bash
./bin/lh-trt-zerocopy convert-frame fixtures/sample_nv12_frame.json --format json
```

---

## 7. Zero External Dependencies Mandate

`lh-tensorrt-zero-copy` adheres to a strict **100% Zero External Dependencies Mandate**:
- Implemented purely in ISO C++17 using standard library headers (`<vector>`, `<string>`, `<cstdint>`, `<memory>`, `<iostream>`, `<fstream>`, `<sstream>`, `<cmath>`).
- Zero third-party JSON parsers (clean-room hand-rolled parser).
- Zero third-party crypto libraries (clean-room FIPS 180-4 SHA-256 implementation).
- Zero CUDA SDK or V4L2 kernel header dependencies required at build-time (clean-room portable hardware abstractions and ioctl layout models).

---

## 8. Governance & Maintainers

- **Author**: Sameer Tawade (`ops@logihertz.com`)
- **Organization**: Logihertz Systems (OPC) Private Limited
- **License**: Apache License, Version 2.0 (see [LICENSE](LICENSE))
- **Security Policy**: See [SECURITY.md](SECURITY.md)
- **Contributing Guidelines**: See [CONTRIBUTING.md](CONTRIBUTING.md)
