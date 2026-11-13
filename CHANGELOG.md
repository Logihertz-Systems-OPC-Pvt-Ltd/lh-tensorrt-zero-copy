# Changelog

All notable changes to `lh-tensorrt-zero-copy` will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - 2026-11-13

### Added
- **Hardware Pitch & SMMU Page Alignment Calculator (`src/dma_buf_pool.cpp`)**:
  - Implemented 256-byte hardware pitch alignment calculation for NV12, UYVY, RGBA8, and planar formats.
  - Implemented 4096-byte SMMU page alignment calculation for unified `DMA-BUF` allocations.
  - Implemented analytical DRAM bus traffic and bandwidth savings model (quantifying 50% DRAM bandwidth reduction vs legacy 3-copy pipelines).
- **Lock-Free `DMA-BUF` Ring-Buffer & Syncpoint Fence State Machine**:
  - Implemented multi-slot (`N_slots` in [3, 16]) atomic state tracker: `SLOT_FREE -> SLOT_V4L2_DMA_CAPTURE -> SLOT_DMA_READY_FENCED -> SLOT_TRT_GPU_INFER -> SLOT_FREE`.
  - Built-in write-while-infer race prevention (`DMA_ERR_WRITE_DURING_INFER`, CWE-362).
  - Built-in hardware syncpoint fence validation (`DMA_ERR_FENCE_NOT_SIGNALED`).
  - Monotonic V4L2 frame sequence number tracking and discontinuity detection.
  - Secure volatile zeroization (`secure_zero_dma_buffer`) on pool destruction.
- **BT.709 `NV12` to `NCHW` Color & Layout Tensor Bridge (`src/color_tensor_bridge.cpp`)**:
  - Semi-planar `NV12` (luma $Y$ + interleaved chroma $UV$) to planar `NCHW` (`[1, 3, H, W]`) transformation.
  - Full-swing and studio-swing ITU-R BT.709 color conversion matrix.
  - Configurable ImageNet / YOLO mean and standard deviation normalization in `FP32`.
  - Calibrated symmetric `INT8` quantization clamping to $[-127, +127]$.
- **10-Rule V4L2-to-TensorRT DMA Posture Auditor (`src/trt_auditor.cpp`)**:
  - Evaluates rules `LH-TRT-001` through `LH-TRT-010` covering ARM SMMU stage-1 isolation, hardware sync fences, engine cryptographic signatures, FD_CLOEXEC limits, pitch geometry, zero-copy enforcement, heap permissions, sequence watchdogs, preallocated execution contexts, and clock locking.
  - Standardized Logihertz Security Posture Score calculation ($S_{\text{posture}} \in [0, 100]$).
- **Multi-Format Exporters (`src/exporters.cpp`)**:
  - ANSI terminal formatted tables with status badges.
  - Canonical JSON exporter.
  - OASIS SARIF v2.1.0 exporter with complete rule catalog and CWE cross-references.
- **Userspace Command-Line Interface (`src/main.cpp` / `bin/lh-trt-zerocopy`)**:
  - Subcommands: `scan`, `calc-layout`, `sim-pipeline`, `convert-frame`, `--version`, and `--help`.
- **Deterministic Test Suite (`tests/test_runner.cpp`)**:
  - Comprehensive suite of 85+ assertions verifying layout math, fence transitions, race prevention, tensor conversion, and auditor evaluations.
