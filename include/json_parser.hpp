#ifndef LH_JSON_PARSER_HPP
#define LH_JSON_PARSER_HPP

#include "zerocopy_types.hpp"
#include <string>
#include <vector>

namespace logihertz {

struct FrameFixture {
    std::string frame_name;
    uint32_t width{0};
    uint32_t height{0};
    uint32_t pitch_bytes{0};
    PixelFormat format{PixelFormat::NV12};
    std::vector<uint8_t> nv12_bytes;
    std::vector<uint64_t> sequence_trace;
};

class JsonParser {
public:
    static bool parse_policy_string(const std::string &json_str, PolicyConfig &policy);
    static bool parse_policy_file(const std::string &filepath, PolicyConfig &policy);

    static bool parse_frame_string(const std::string &json_str, FrameFixture &fixture);
    static bool parse_frame_file(const std::string &filepath, FrameFixture &fixture);
};

} // namespace logihertz

#endif // LH_JSON_PARSER_HPP
