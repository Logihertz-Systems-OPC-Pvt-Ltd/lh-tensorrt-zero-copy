# Contributing to Logihertz V4L2-to-TensorRT Zero-Copy DMA Allocator

Thank you for your interest in contributing to `lh-tensorrt-zero-copy`. This project provides a production-grade C++17 V4L2-to-TensorRT zero-copy `DMA-BUF` shared memory allocator, hardware pitch and SMMU page alignment engine, lock-free syncpoint fence state machine, BT.709 YUV-to-NCHW tensor bridge, and 10-rule edge vision DMA posture auditor.

## Architectural Mandates & Principles

1. **Zero Third-Party Library Dependencies**:
   - The CLI engine (`bin/lh-trt-zerocopy`) and test runner (`tests/test_runner`) must depend exclusively on the ISO C++17 Standard Library (`std`).
   - No external frameworks, CUDA runtime libraries, or third-party JSON parsers are permitted in the core verification toolchain.
2. **Strict Compiler Conformance**:
   - Code must compile with zero warnings under both GCC (`g++`) and Clang (`clang++`):
     ```bash
     -std=c++17 -O3 -Wall -Wextra -Werror -pedantic
     ```
3. **Deterministic Memory Safety & Hardware Geometry**:
   - Surface pitch alignment must adhere strictly to 256-byte multiples ($A_{\text{pitch}} = 256$) required by embedded vision hardware (NVIDIA Tegra VIC / NVCSI).
   - Total buffer allocations must be padded to 4096-byte SMMU page boundaries ($A_{\text{page}} = 4096$).
4. **Race-Free Synchronization Invariants**:
   - Enforce lock-free state transitions protecting against write-during-infer tearing (`CWE-362`).
   - All DMA buffer memory must be sanitized on deallocation.

## Development Workflow

### Prerequisites

- GCC 9+ (`g++`) or Clang 11+ (`clang++`)
- GNU Make 4.0+
- Standard POSIX libc and C++17 Standard Library

### Building and Testing

```bash
# Build binary and run test suite
make all test

# Build with Clang (if installed)
make CXX=clang++ clean all test

# Clean artifacts
make clean
```

### Submitting Pull Requests

1. Fork the repository and create a feature branch (`git checkout -b feature/smmu-guard`).
2. Adhere to Conventional Commits:
   - `feat(dmabuf): add 64-byte aligned sub-allocation tracker`
   - `fix(fence): prevent timeout race on high-frame-rate streams`
   - `test(bridge): add test assertion for BT.709 full-swing saturation`
3. Ensure `make all test` passes with 100% success and zero warnings.
4. Open a pull request against the `main` branch.
