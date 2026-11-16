# Engineering & Technical Architecture Specification

**Specification ID**: `LH-SPEC-2026-056`  
**Repository**: `lh-tensorrt-zero-copy`  
**CLI Executable**: `lh-trt-zerocopy`  
**Standard Release Version**: `0.1.0`  
**Standard License**: `Apache-2.0`  
**Classification**: `Industrial Sovereign Cybersecurity & Critical Infrastructure Telemetry`

---

## Section 1: Document Control & Specification Metadata

| Metadata Attribute | Value |
| :--- | :--- |
| **Document Identification** | `LH-SPEC-2026-056` |
| **System Name** | `Logihertz Systems Sovereign Ecosystem` |
| **Target Component** | `lh-tensorrt-zero-copy` (`lh-trt-zerocopy`) |
| **Target Language / Platform** | `C++` |
| **Legal Entity** | Logihertz Systems (OPC) Private Limited |
| **Corporate Identity (CIN)** | `U72100MH2025OPC463857` |
| **GSTIN Registration** | `27AAGCL6394P1ZL` |
| **Principal Architect** | Sameer Tawade (`ops@logihertz.com` / `security@logihertz.com`) |
| **Headquarters & Registered Office** | Mumbai, Maharashtra, Republic of India |
| **Official Portal** | [https://www.logihertz.com](https://www.logihertz.com) |
| **Repository URL** | [https://github.com/Logihertz-Systems-OPC-Pvt-Ltd/lh-tensorrt-zero-copy](https://github.com/Logihertz-Systems-OPC-Pvt-Ltd/lh-tensorrt-zero-copy) |
| **Document Status** | `APPROVED FOR PRODUCTION & INDUSTRIAL CERTIFICATION` |
| **Effective Release Date** | `2026-11-16` |

---

## Section 2: Normative RFC 2119 Requirements & System Invariants

This technical architecture specification enforces strict deterministic execution invariants in accordance with **RFC 2119** (`MUST`, `MUST NOT`, `REQUIRED`, `SHALL`, `SHALL NOT`, `SHOULD`, `RECOMMENDED`, `MAY`).

1. **Zero-Daemon Execution Invariant**:
   - The CLI binary `lh-trt-zerocopy` `MUST` execute as a clean, self-contained, stateless, zero-daemon process.
   - It `MUST NOT` spawn background daemons, fork unmonitored worker processes, or establish persistent Unix domain socket listeners unless explicitly invoked in daemon mode.
   - All state transitions `MUST` be deterministic, ephemeral, and strictly bounded by the execution lifecycle.

2. **Zero External Runtime Dependency Mandate**:
   - The software `MUST NOT` load, link, or depend upon third-party runtime package registries (e.g. PyPI, npm, Packagist, crates.io, Maven) at runtime.
   - For `C++`, all cryptographic, mathematical, serialization, and networking primitives `MUST` be implemented exclusively via language standard libraries or clean-room internal zero-dependency modules.

3. **Deterministic Air-Gapped Operation**:
   - The system `MUST` function with 100% feature parity in air-gapped, isolated, and offline enclaves.
   - The software `MUST NOT` initiate unsolicited network calls, telemetry telemetry pings, phone-home beacons, or external CDN queries.
   - All verification baselines, heuristics, and schemas `MUST` be packaged locally within the distribution.

4. **Constant-Time Cryptographic Comparison**:
   - All cryptographic comparison routines (hashes, HMAC tags, signatures, secret tokens, and commitments) `MUST` execute in constant time (`subtle.ConstantTimeCompare`, `CRYPTO_memcmp`, or equivalent).
   - The implementation `MUST NOT` short-circuit string comparisons when evaluating sensitive keys, thereby mitigating timing side-channel attacks.

5. **Memory Safety & Explicit Zeroization**:
   - All transient buffers containing sensitive key material, decrypted plaintexts, seeds, and cryptographic nonces `MUST` be explicitly overwritten with zeroes immediately following completion of the cryptographic operation.
   - For C/C++ implementations, bounds checks and safe buffer wrappers `MUST` prevent stack/heap overflows. For Rust implementations, `#![forbid(unsafe_code)]` `MUST` be enforced.

6. **Standardized Telemetry Interoperability**:
   - All security assessment outputs `MUST` support machine-readable ingestion via OASIS SARIF v2.1.0 JSON and Canonical Indented JSON schemas, alongside ANSI console tables.

---

## Section 3: Core Mathematical & Algorithmic Specification

### 3.1 Domain Engine Mathematical Foundation: Hardware Pitch Alignment & SMMU Page Floor Invariant

The underlying computational core of `lh-tensorrt-zero-copy` is formally governed by the following mathematical and algorithmic formulations:

$$\text{Pitch} = \left\lceil \frac{W \cdot \text{bpp}}{256} \right\rceil \times 256, \quad \text{Addr}_{\text{buf}} \equiv 0 \pmod{4096}$$

### 3.2 Standardized Logihertz Security Posture Score Formulation

To achieve absolute cross-tool normalization across all 60 repositories in the Logihertz Systems Ecosystem, the security posture score $S_{\text{posture}}$ is evaluated on a standardized continuous integer scale $[0, 100]$:

$$S_{\text{posture}} = \max\left(0, \; 100 - \left(25 \cdot c_{\text{CRITICAL}} + 10 \cdot c_{\text{HIGH}} + 4 \cdot c_{\text{MEDIUM}} + 1 \cdot c_{\text{LOW}}\right)\right)$$

Where:
- $c_{\text{CRITICAL}}$ represents the total count of triggered Critical-severity normative rules (deduction: $-25$ per finding).
- $c_{\text{HIGH}}$ represents the total count of triggered High-severity normative rules (deduction: $-10$ per finding).
- $c_{\text{MEDIUM}}$ represents the total count of triggered Medium-severity normative rules (deduction: $-4$ per finding).
- $c_{\text{LOW}}$ represents the total count of triggered Low-severity normative rules (deduction: $-1$ per finding).

The final compliance verdict is strictly bifurcated:
$$\text{Verdict} = \begin{cases} \text{PASS} & \text{if } S_{\text{posture}} = 100 \land \sum c_i = 0 \\ \text{FAIL} & \text{otherwise} \end{cases}$$

---

## Section 4: Normative 10-Rule Detection Matrix

The detection engine evaluates configurations, manifests, firmware, protocols, and streams against the following 10 deterministic security rules:

| Rule ID | Severity | Weight | CWE ID | Trigger Predicate / Condition | Remediation Guideline |
| :--- | :--- | :---: | :---: | :--- | :--- |
| `LH-TRT-001` | `CRITICAL` | `25` | `CWE-119` | CWE-119 | Hardware IOMMU / ARM SMMU Stage-1 DMA Isolation: Enforces physical address isolation between camera ISP and GPU, prohibiting arbitrary bus-master physical DMA exploitation. |
| `LH-TRT-002` | `CRITICAL` | `25` | `CWE-362` | CWE-362 | Hardware Syncpoint Fence & Write-While-Infer Race Block: Enforces hardware syncpoint fences and blocks queueing camera capture buffers while actively locked by TensorRT inference (prevents frame tearing). |
| `LH-TRT-003` | `CRITICAL` | `25` | `CWE-502` | CWE-502 | Cryptographic Engine Verification & Untrusted Plugin Block: Prohibits loading unsigned serialized TensorRT .plan/.engine files and blocks untrusted dynamic plugin shared libraries (.so). |
| `LH-TRT-004` | `HIGH` | `10` | `CWE-400` | CWE-400 | DMA-BUF File Descriptor FD_CLOEXEC & Safe Pool Sizing: Enforces close-on-exec flag on all exported DMA-BUF file descriptors and restricts pool sizing within safe bounds $[3 \dots 16]$ to prevent resource starvation. |
| `LH-TRT-005` | `HIGH` | `10` | `CWE-125` | CWE-125 | Hardware Pitch (256B) & SMMU Page (4KB) Geometry Bounds: Validates line pitch $\ge 256\text{B}$ and total surface buffer $\ge 4096\text{B}$ to prevent out-of-bounds DMA access faults. |
| `LH-TRT-006` | `HIGH` | `10` | `CWE-400` | CWE-400 | True Zero-Copy DMA-BUF Enforcement & CPU memcpy Block: Mandates native DMA-BUF unified memory paths, strictly forbidding CPU memcpy staging fallbacks that incur 50% extra DRAM traffic. |
| `LH-TRT-007` | `HIGH` | `10` | `CWE-226` | CWE-226 | DMA-BUF Zeroization on Free & Restricted Heap Permissions: Sanitizes volatile shared DMA memory on release to prevent remanence leakage and enforces /dev/dma_heap permissions 0600. |
| `LH-TRT-008` | `MEDIUM` | `4` | `CWE-662` | CWE-662 | MIPI CSI Frame-Timeout Watchdog & Monotonic Sequence Check: Detects CSI desynchronization and packet drops with hardware watchdogs $\le 100\text{ ms}$ and monotonic sequence verification. |
| `LH-TRT-009` | `MEDIUM` | `4` | `CWE-400` | CWE-400 | Preallocated Execution Contexts & Memory Locking (mlock): Preallocates TensorRT scratch memory and locks DMA pool metadata pages in RAM via mlock() to eliminate dynamic paging latency spikes. |
| `LH-TRT-010` | `LOW` | `1` | `CWE-778` | CWE-778 | End-to-End Latency Telemetry & Jetson Clock Lock Guard: Emits high-precision nanosecond capture-to-inference latency timestamps and verifies EMC/GPU clocks are pinned via jetson_clocks. |

---

## Section 5: CLI Contract, Exit Codes & OASIS SARIF v2.1.0 Specification

### 5.1 Command-Line Interface Syntax

The primary entrypoint `lh-trt-zerocopy` conforms to standard POSIX argument conventions:

```text
USAGE:
  lh-trt-zerocopy <command> [flags] [arguments]

CORE COMMANDS:
  scan <target> [--format table|json|sarif] [--output <file>]
      Execute deterministic security evaluation against target manifest or telemetry stream.
  verify <target>
      Validate operational parameters, cryptographic checksums, and structural invariants.
  --version, version
      Output release version, architecture, and legal attribution.
  --help, help
      Display comprehensive operational usage and command reference.
```

### 5.2 Deterministic Exit Code Protocol

| Exit Code | Classification | Operational Semantics |
| :---: | :--- | :--- |
| `0` | **SUCCESS / PASS** | Target passed all checks; zero Critical/High violations detected; $S_{\text{posture}} = 100$. |
| `1` | **FINDINGS / FAIL** | Target triggered one or more security rule violations; policy threshold breached. |
| `2` | **ERROR / USAGE** | Invalid CLI invocation, syntax error, missing input file, or fatal I/O failure. |

### 5.3 OASIS SARIF v2.1.0 Schema Compliance

Exported SARIF documents validate against the official OASIS schema (`sarif-schema-2.1.0.json`). Each result object maps:
- `ruleId`: Standardized identifier (`LH-TRT-001` .. `LH-TRT-010`).
- `level`: SARIF severity levels (`error` for Critical/High, `warning` for Medium, `note` for Low).
- `message.text`: Comprehensive explanation, CWE mapping, and actionable remediation steps.
- `locations[].physicalLocation.artifactLocation.uri`: Absolute or normalized relative path of target artifact.

---

## Section 6: Future Development Roadmap & Extension Architecture

### Milestone v0.2.0: Modular Extensions & Telemetry Streaming
- **Policy Pack Plugin Hooks**: Enable external declarative YAML/JSON rule-pack loading without source modification.
- **Hardware Telemetry Streaming**: Real-time integration with SPI, I2C, UART, CAN, and eBPF kernel event rings.
- **Configurable Severity Weights**: Support custom industrial profiles (e.g. Nuclear/Medical safety vs. Commercial IoT).

### Milestone v0.5.0: Native STRUCT OS Integration & Daemonless IPC
- **STRUCT OS Security Bus**: Native zero-copy shared memory integration with the STRUCT OS Security Command Center.
- **Automated Remediation Synthesis**: Direct IPC bridge with `lh-auto-remediation` for automatic synthesizing of verified unified diffs.
- **Hardware Root-of-Trust Attestation**: Cryptographic sealing of telemetry reports via TPM 2.0 PCR-bound quotes.

### Milestone v1.0.0: High-Assurance Certification & Hardening
- **FIPS 140-3 Validation**: Cryptographic module boundary verification and NIST CAVP compliance testing.
- **Common Criteria EAL4+ Alignment**: Formal verification of deterministic security targets and functional invariants.
- **Automotive & Industrial Compliance**: Formal baseline compliance with ISO 21434 (Automotive Cybersecurity) and IEC 62443 (Industrial Automation & Control Systems).
- **Signed Binary Attestation**: In-toto attestation and Cosign cryptographic signing across all release artifacts.

---

## Section 7: Verification, Quality Assurance & Legal Attribution

### 7.1 Automated Verification Protocol

Prior to distribution, all builds must execute the standardized Logihertz test suite:
- Unit and mathematical property tests verifying all equations and edge cases.
- Static analysis with zero warnings under standard strict linters.
- Subprocess CLI verification covering exit codes `0`, `1`, and `2`.

### 7.2 Copyright & Governance Notice

```text
Copyright 2025-2026 Logihertz Systems (OPC) Private Limited.
Creator, Founder, Director & Sole Developer: Sameer Tawade <ops@logihertz.com> / <security@logihertz.com>
Official Corporate Portal: https://www.logihertz.com
```

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this software except in compliance with the License. You may obtain a copy of the License at `http://www.apache.org/licenses/LICENSE-2.0`.
