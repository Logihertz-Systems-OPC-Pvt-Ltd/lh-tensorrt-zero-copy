#include "json_parser.hpp"
#include <fstream>
#include <sstream>
#include <cctype>
#include <cstdlib>

namespace logihertz {

static void skip_ws(const std::string &s, size_t &pos) {
    while (pos < s.size() && (std::isspace(static_cast<unsigned char>(s[pos])) || s[pos] == ',' || s[pos] == ':')) {
        pos++;
    }
}

static size_t find_key(const std::string &json, const std::string &key) {
    std::string needle = "\"" + key + "\"";
    size_t p = json.find(needle);
    while (p != std::string::npos) {
        size_t after = p + needle.size();
        while (after < json.size() && std::isspace(static_cast<unsigned char>(json[after]))) after++;
        if (after < json.size() && json[after] == ':') {
            after++;
            while (after < json.size() && std::isspace(static_cast<unsigned char>(json[after]))) after++;
            return after;
        }
        p = json.find(needle, p + 1);
    }
    return std::string::npos;
}

static bool extract_string(const std::string &json, const std::string &key, std::string &out) {
    size_t p = find_key(json, key);
    if (p == std::string::npos) return false;
    skip_ws(json, p);
    if (p >= json.size() || json[p] != '"') return false;
    p++;
    size_t end = json.find('"', p);
    if (end == std::string::npos) return false;
    out = json.substr(p, end - p);
    return true;
}

static bool extract_bool(const std::string &json, const std::string &key, bool &out) {
    size_t p = find_key(json, key);
    if (p == std::string::npos) return false;
    skip_ws(json, p);
    if (json.compare(p, 4, "true") == 0) {
        out = true;
        return true;
    }
    if (json.compare(p, 5, "false") == 0) {
        out = false;
        return true;
    }
    return false;
}

static bool extract_uint32(const std::string &json, const std::string &key, uint32_t &out) {
    size_t p = find_key(json, key);
    if (p == std::string::npos) return false;
    skip_ws(json, p);
    char *endp = nullptr;
    const char *start = json.c_str() + p;
    long val = 0;
    if (start[0] == '"') {
        start++;
        if (start[0] == '0' && std::isdigit(static_cast<unsigned char>(start[1]))) {
            val = std::strtol(start, &endp, 8);
        } else {
            val = std::strtol(start, &endp, 10);
        }
    } else {
        val = std::strtol(start, &endp, 10);
    }
    if (endp == start) return false;
    out = static_cast<uint32_t>(val);
    return true;
}

bool JsonParser::parse_policy_string(const std::string &json_str, PolicyConfig &policy) {
    extract_string(json_str, "policy_name", policy.policy_name);
    extract_bool(json_str, "iommu_smmu_isolation_enabled", policy.iommu_smmu_isolation_enabled);
    extract_bool(json_str, "allow_unrestricted_physical_dma", policy.allow_unrestricted_physical_dma);
    extract_bool(json_str, "hardware_sync_fence_enforced", policy.hardware_sync_fence_enforced);
    extract_bool(json_str, "allow_concurrent_dma_write_during_infer", policy.allow_concurrent_dma_write_during_infer);
    extract_bool(json_str, "verify_trt_engine_signature", policy.verify_trt_engine_signature);
    extract_bool(json_str, "allow_untrusted_trt_plugins", policy.allow_untrusted_trt_plugins);
    extract_bool(json_str, "dmabuf_fd_cloexec_enforced", policy.dmabuf_fd_cloexec_enforced);
    extract_uint32(json_str, "dmabuf_pool_slots", policy.dmabuf_pool_slots);
    extract_uint32(json_str, "pitch_alignment_bytes", policy.pitch_alignment_bytes);
    extract_uint32(json_str, "page_alignment_bytes", policy.page_alignment_bytes);
    extract_bool(json_str, "strict_plane_bounds_check", policy.strict_plane_bounds_check);
    extract_bool(json_str, "zero_copy_dmabuf_enabled", policy.zero_copy_dmabuf_enabled);
    extract_bool(json_str, "allow_cpu_memcpy_fallback", policy.allow_cpu_memcpy_fallback);
    extract_bool(json_str, "zeroize_dmabuf_on_free", policy.zeroize_dmabuf_on_free);
    extract_uint32(json_str, "dma_heap_permissions_octal", policy.dma_heap_permissions_octal);
    extract_uint32(json_str, "csi_frame_timeout_ms", policy.csi_frame_timeout_ms);
    extract_bool(json_str, "verify_monotonic_v4l2_sequence", policy.verify_monotonic_v4l2_sequence);
    extract_bool(json_str, "preallocate_trt_execution_context", policy.preallocate_trt_execution_context);
    extract_bool(json_str, "mlock_dma_pool_buffers", policy.mlock_dma_pool_buffers);
    extract_bool(json_str, "log_e2e_latency_telemetry", policy.log_e2e_latency_telemetry);
    extract_bool(json_str, "verify_jetson_clocks_locked", policy.verify_jetson_clocks_locked);
    return true;
}

bool JsonParser::parse_policy_file(const std::string &filepath, PolicyConfig &policy) {
    std::ifstream ifs(filepath);
    if (!ifs.is_open()) return false;
    std::string str((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    return parse_policy_string(str, policy);
}

bool JsonParser::parse_frame_string(const std::string &json_str, FrameFixture &fixture) {
    extract_string(json_str, "frame_name", fixture.frame_name);
    extract_uint32(json_str, "width", fixture.width);
    extract_uint32(json_str, "height", fixture.height);
    extract_uint32(json_str, "pitch_bytes", fixture.pitch_bytes);

    if (fixture.width == 0) fixture.width = 16;
    if (fixture.height == 0) fixture.height = 16;
    if (fixture.pitch_bytes == 0) fixture.pitch_bytes = 256;

    fixture.format = PixelFormat::NV12;

    /* Parse nv12_samples array */
    size_t p = find_key(json_str, "nv12_samples");
    if (p != std::string::npos) {
        size_t start_bracket = json_str.find('[', p);
        if (start_bracket != std::string::npos) {
            size_t end_bracket = json_str.find(']', start_bracket);
            if (end_bracket != std::string::npos) {
                size_t curr = start_bracket + 1;
                while (curr < end_bracket) {
                    skip_ws(json_str, curr);
                    if (curr >= end_bracket) break;
                    char *endp = nullptr;
                    long val = std::strtol(json_str.c_str() + curr, &endp, 0);
                    if (endp == json_str.c_str() + curr) {
                        curr++;
                        continue;
                    }
                    fixture.nv12_bytes.push_back(static_cast<uint8_t>(val & 0xff));
                    curr = static_cast<size_t>(endp - json_str.c_str());
                }
            }
        }
    }

    /* Fallback if no samples: generate deterministic NV12 bytes */
    size_t needed = fixture.pitch_bytes * fixture.height + fixture.pitch_bytes * (fixture.height / 2);
    if (fixture.nv12_bytes.size() < needed) {
        fixture.nv12_bytes.resize(needed, 128); /* Neutral gray luma and chroma */
        for (uint32_t y = 0; y < fixture.height; y++) {
            for (uint32_t x = 0; x < fixture.width; x++) {
                fixture.nv12_bytes[y * fixture.pitch_bytes + x] = static_cast<uint8_t>((x * 13 + y * 7 + 16) % 220);
            }
        }
    }

    /* Parse sequence_trace array */
    p = find_key(json_str, "sequence_trace");
    if (p != std::string::npos) {
        size_t start_bracket = json_str.find('[', p);
        if (start_bracket != std::string::npos) {
            size_t end_bracket = json_str.find(']', start_bracket);
            if (end_bracket != std::string::npos) {
                size_t curr = start_bracket + 1;
                while (curr < end_bracket) {
                    skip_ws(json_str, curr);
                    if (curr >= end_bracket) break;
                    char *endp = nullptr;
                    unsigned long long val = std::strtoull(json_str.c_str() + curr, &endp, 0);
                    if (endp == json_str.c_str() + curr) {
                        curr++;
                        continue;
                    }
                    fixture.sequence_trace.push_back(static_cast<uint64_t>(val));
                    curr = static_cast<size_t>(endp - json_str.c_str());
                }
            }
        }
    }

    if (fixture.sequence_trace.empty()) {
        fixture.sequence_trace = { 101, 102, 103, 104, 105 };
    }

    return true;
}

bool JsonParser::parse_frame_file(const std::string &filepath, FrameFixture &fixture) {
    std::ifstream ifs(filepath);
    if (!ifs.is_open()) return false;
    std::string str((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    return parse_frame_string(str, fixture);
}

} // namespace logihertz
