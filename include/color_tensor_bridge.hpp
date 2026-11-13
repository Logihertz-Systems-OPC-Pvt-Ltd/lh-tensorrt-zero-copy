#ifndef LH_COLOR_TENSOR_BRIDGE_HPP
#define LH_COLOR_TENSOR_BRIDGE_HPP

#include "zerocopy_types.hpp"
#include <vector>
#include <string>

namespace logihertz {

struct NormalizationParams {
    float mean[3]{0.485f, 0.456f, 0.406f};   /* ImageNet default R, G, B */
    float std[3]{0.229f, 0.224f, 0.225f};    /* ImageNet default R, G, B */
    bool full_swing_bt709{false};             /* false = Studio swing [16..235], true = Full swing [0..255] */
    float int8_calibration_scale{0.02f};      /* Scale factor for INT8 symmetric quantization */
};

class ColorTensorBridge {
public:
    static NCHWTensor convert_nv12_to_nchw(
        const uint8_t *nv12_data,
        uint32_t width,
        uint32_t height,
        uint32_t pitch_bytes,
        const NormalizationParams &norm = NormalizationParams()
    );

    static std::string compute_tensor_digest(const NCHWTensor &tensor);
};

} // namespace logihertz

#endif // LH_COLOR_TENSOR_BRIDGE_HPP
