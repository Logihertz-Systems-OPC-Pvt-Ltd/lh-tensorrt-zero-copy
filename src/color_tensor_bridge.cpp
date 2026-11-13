#include "color_tensor_bridge.hpp"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace logihertz {

static inline float clamp_f(float v, float lo, float hi) {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

NCHWTensor ColorTensorBridge::convert_nv12_to_nchw(
    const uint8_t *nv12_data,
    uint32_t width,
    uint32_t height,
    uint32_t pitch_bytes,
    const NormalizationParams &norm
) {
    NCHWTensor tensor;
    tensor.shape.batch = 1;
    tensor.shape.channels = 3;
    tensor.shape.height = static_cast<int>(height);
    tensor.shape.width = static_cast<int>(width);
    tensor.int8_scale = norm.int8_calibration_scale;

    size_t plane_elements = static_cast<size_t>(width * height);
    size_t total_elements = 3 * plane_elements;

    tensor.data_fp32.resize(total_elements, 0.0f);
    tensor.data_int8.resize(total_elements, 0);

    if (!nv12_data || width == 0 || height == 0 || pitch_bytes < width) {
        return tensor;
    }

    size_t uv_plane_offset = static_cast<size_t>(pitch_bytes * height);

    for (uint32_t y = 0; y < height; y++) {
        const uint8_t *y_line = nv12_data + (y * pitch_bytes);
        const uint8_t *uv_line = nv12_data + uv_plane_offset + ((y / 2) * pitch_bytes);

        for (uint32_t x = 0; x < width; x++) {
            uint8_t y_val = y_line[x];
            uint32_t uv_col = (x / 2) * 2;
            uint8_t u_val = uv_line[uv_col];
            uint8_t v_val = uv_line[uv_col + 1];

            float r_f = 0.0f, g_f = 0.0f, b_f = 0.0f;

            if (!norm.full_swing_bt709) {
                /* Studio Swing BT.709 */
                float y_norm = static_cast<float>(y_val) - 16.0f;
                float u_norm = static_cast<float>(u_val) - 128.0f;
                float v_norm = static_cast<float>(v_val) - 128.0f;

                r_f = 1.164383f * y_norm + 1.792741f * v_norm;
                g_f = 1.164383f * y_norm - 0.213249f * u_norm - 0.532909f * v_norm;
                b_f = 1.164383f * y_norm + 2.112402f * u_norm;
            } else {
                /* Full Swing BT.709 */
                float y_norm = static_cast<float>(y_val);
                float u_norm = static_cast<float>(u_val) - 128.0f;
                float v_norm = static_cast<float>(v_val) - 128.0f;

                r_f = y_norm + 1.57480f * v_norm;
                g_f = y_norm - 0.18732f * u_norm - 0.46812f * v_norm;
                b_f = y_norm + 1.85560f * u_norm;
            }

            r_f = clamp_f(r_f, 0.0f, 255.0f);
            g_f = clamp_f(g_f, 0.0f, 255.0f);
            b_f = clamp_f(b_f, 0.0f, 255.0f);

            float rgb[3] = { r_f, g_f, b_f };

            size_t spatial_idx = static_cast<size_t>(y * width + x);

            for (int c = 0; c < 3; c++) {
                float norm_val = (rgb[c] / 255.0f - norm.mean[c]) / norm.std[c];
                size_t tensor_idx = static_cast<size_t>(c * plane_elements + spatial_idx);

                tensor.data_fp32[tensor_idx] = norm_val;

                /* INT8 symmetric quantization */
                float q = std::round(norm_val / norm.int8_calibration_scale);
                int q_clamped = std::clamp(static_cast<int>(q), -127, 127);
                tensor.data_int8[tensor_idx] = static_cast<int8_t>(q_clamped);
            }
        }
    }

    tensor.sha256_digest = compute_tensor_digest(tensor);
    return tensor;
}

/* Self-contained FIPS 180-4 SHA-256 for deterministic tensor verification */
static inline uint32_t ror32(uint32_t v, unsigned int s) {
    return (v >> s) | (v << (32 - s));
}

static const uint32_t k256[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

std::string ColorTensorBridge::compute_tensor_digest(const NCHWTensor &tensor) {
    if (tensor.data_fp32.empty()) return "0000000000000000000000000000000000000000000000000000000000000000";

    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(tensor.data_fp32.data());
    size_t byte_len = tensor.data_fp32.size() * sizeof(float);

    uint32_t state[8] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
    };

    uint8_t buffer[64];
    size_t offset = 0;
    uint64_t total_bits = static_cast<uint64_t>(byte_len) * 8ULL;

    auto transform = [&](const uint8_t blk[64]) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++) {
            w[i] = (static_cast<uint32_t>(blk[i * 4]) << 24) |
                   (static_cast<uint32_t>(blk[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(blk[i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(blk[i * 4 + 3]));
        }
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = ror32(w[i - 15], 7) ^ ror32(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = ror32(w[i - 2], 17) ^ ror32(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

        for (int i = 0; i < 64; i++) {
            uint32_t s1 = ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = h + s1 + ch + k256[i] + w[i];
            uint32_t s0 = ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = s0 + maj;

            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    };

    while (offset + 64 <= byte_len) {
        transform(bytes + offset);
        offset += 64;
    }

    size_t rem = byte_len - offset;
    std::memcpy(buffer, bytes + offset, rem);
    buffer[rem++] = 0x80;

    if (rem > 56) {
        std::memset(buffer + rem, 0, 64 - rem);
        transform(buffer);
        rem = 0;
    }
    std::memset(buffer + rem, 0, 56 - rem);

    for (int i = 0; i < 8; i++) {
        buffer[56 + i] = static_cast<uint8_t>((total_bits >> ((7 - i) * 8)) & 0xff);
    }
    transform(buffer);

    std::ostringstream ss;
    for (int i = 0; i < 8; i++) {
        ss << std::hex << std::setw(8) << std::setfill('0') << state[i];
    }
    return ss.str();
}

} // namespace logihertz
