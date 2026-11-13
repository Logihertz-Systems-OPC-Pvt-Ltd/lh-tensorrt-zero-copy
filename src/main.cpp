#include "zerocopy_types.hpp"
#include "dma_buf_pool.hpp"
#include "color_tensor_bridge.hpp"
#include "trt_auditor.hpp"
#include "exporters.hpp"
#include "json_parser.hpp"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>

using namespace logihertz;

static void print_version() {
    std::cout << "lh-trt-zerocopy 0.1.0\n";
    std::cout << "Logihertz V4L2-to-TensorRT Zero-Copy DMA Allocator & Security Posture Auditor\n";
    std::cout << "Copyright 2026 Logihertz Systems (OPC) Private Limited\n";
    std::cout << "Author: Sameer Tawade <ops@logihertz.com>\n";
}

static void print_help(const char *prog) {
    print_version();
    std::cout << "\nUsage:\n";
    std::cout << "  " << prog << " <subcommand> [options]\n\n";
    std::cout << "Subcommands:\n";
    std::cout << "  scan <policy.json>                Audit V4L2-to-TensorRT DMA posture against 10 rules\n";
    std::cout << "  calc-layout --width <W> --height <H> Calculate 256B pitch & 4KB SMMU layout geometry\n";
    std::cout << "  sim-pipeline <frame.json>         Simulate lock-free V4L2->SyncFence->TRT DMA pipeline\n";
    std::cout << "  convert-frame <frame.json>        Convert BT.709 NV12 frame to normalized NCHW tensor\n";
    std::cout << "  --version, -v                     Display version and maintainer information\n";
    std::cout << "  --help, -h                        Display this command manual\n\n";
    std::cout << "Options for 'scan':\n";
    std::cout << "  --format <table|json|sarif>       Output formatting (default: table)\n";
    std::cout << "  --output <filepath>               Write output report to file instead of stdout\n\n";
    std::cout << "Options for 'calc-layout':\n";
    std::cout << "  --width <pixels>                  Frame width in pixels (e.g. 1920)\n";
    std::cout << "  --height <pixels>                 Frame height in pixels (e.g. 1080)\n";
    std::cout << "  --pixel-format <NV12|UYVY|RGBA8>  Input camera pixel format (default: NV12)\n";
    std::cout << "  --fps <framerate>                 Target camera framerate in FPS (default: 60.0)\n";
    std::cout << "  --pitch-align <bytes>             Hardware pitch alignment (default: 256)\n";
    std::cout << "  --format <table|json>             Output formatting (default: table)\n";
    std::cout << "  --output <filepath>               Write output to file instead of stdout\n\n";
    std::cout << "Options for 'sim-pipeline':\n";
    std::cout << "  --slots <num>                     Number of DMA ring slots (default: 4)\n";
    std::cout << "  --format <table|json>             Output formatting (default: table)\n";
    std::cout << "  --output <filepath>               Write output to file instead of stdout\n\n";
    std::cout << "Options for 'convert-frame':\n";
    std::cout << "  --full-swing                      Use full-swing [0..255] BT.709 color conversion\n";
    std::cout << "  --format <table|json>             Output formatting (default: table)\n";
    std::cout << "  --output <filepath>               Write output to file instead of stdout\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << prog << " scan fixtures/hardened_zerocopy_policy.json --format table\n";
    std::cout << "  " << prog << " calc-layout --width 1920 --height 1080 --fps 60\n";
    std::cout << "  " << prog << " sim-pipeline fixtures/sample_nv12_frame.json\n";
    std::cout << "  " << prog << " convert-frame fixtures/sample_nv12_frame.json --format json\n";
}

static int handle_scan(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "Error: 'scan' requires a policy JSON file argument.\n";
        return 2;
    }

    std::string policy_file = argv[2];
    std::string format = "table";
    std::string output_file;

    for (int i = 3; i < argc; i++) {
        if (std::strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            format = argv[++i];
        } else if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else {
            std::cerr << "Unknown option for scan: " << argv[i] << "\n";
            return 2;
        }
    }

    PolicyConfig policy;
    if (!JsonParser::parse_policy_file(policy_file, policy)) {
        std::cerr << "Error: Failed to parse policy file: " << policy_file << "\n";
        return 2;
    }

    AuditReport report = TrtAuditor::evaluate_posture(policy);

    std::ofstream ofs;
    std::ostream *out_stream = &std::cout;
    if (!output_file.empty()) {
        ofs.open(output_file);
        if (!ofs.is_open()) {
            std::cerr << "Error: Unable to open output file: " << output_file << "\n";
            return 2;
        }
        out_stream = &ofs;
    }

    if (format == "json") {
        Exporters::export_audit_json(report, *out_stream);
    } else if (format == "sarif") {
        Exporters::export_audit_sarif(report, *out_stream);
    } else {
        Exporters::export_audit_table(report, *out_stream);
    }

    if (!output_file.empty()) {
        std::cout << "Report written to " << output_file << "\n";
    }

    return (report.posture_score >= 80) ? 0 : 1;
}

static int handle_calc_layout(int argc, char **argv) {
    uint32_t width = 1920;
    uint32_t height = 1080;
    PixelFormat format = PixelFormat::NV12;
    double fps = 60.0;
    size_t pitch_align = TEGRA_PITCH_ALIGNMENT;
    std::string out_format = "table";
    std::string output_file;

    for (int i = 2; i < argc; i++) {
        if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            width = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            height = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (std::strcmp(argv[i], "--pixel-format") == 0 && i + 1 < argc) {
            std::string fmt_str = argv[++i];
            if (fmt_str == "UYVY") format = PixelFormat::UYVY;
            else if (fmt_str == "RGBA8") format = PixelFormat::RGBA8;
            else format = PixelFormat::NV12;
        } else if (std::strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
            fps = std::stod(argv[++i]);
        } else if (std::strcmp(argv[i], "--pitch-align") == 0 && i + 1 < argc) {
            pitch_align = std::stoul(argv[++i]);
        } else if (std::strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            out_format = argv[++i];
        } else if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        }
    }

    DmaPlaneLayout layout = calculate_dma_surface_layout(width, height, format, fps, pitch_align);

    std::ofstream ofs;
    std::ostream *out_stream = &std::cout;
    if (!output_file.empty()) {
        ofs.open(output_file);
        if (!ofs.is_open()) {
            std::cerr << "Error: Unable to open output file: " << output_file << "\n";
            return 2;
        }
        out_stream = &ofs;
    }

    if (out_format == "json") {
        Exporters::export_layout_json(layout, *out_stream);
    } else {
        Exporters::export_layout_table(layout, *out_stream);
    }

    if (!output_file.empty()) {
        std::cout << "Layout analysis written to " << output_file << "\n";
    }

    return 0;
}

static int handle_sim_pipeline(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "Error: 'sim-pipeline' requires a frame JSON fixture.\n";
        return 2;
    }

    std::string frame_file = argv[2];
    size_t slots = 4;
    std::string format = "table";
    std::string output_file;

    for (int i = 3; i < argc; i++) {
        if (std::strcmp(argv[i], "--slots") == 0 && i + 1 < argc) {
            slots = std::stoul(argv[++i]);
        } else if (std::strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            format = argv[++i];
        } else if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        }
    }

    FrameFixture fixture;
    if (!JsonParser::parse_frame_file(frame_file, fixture)) {
        std::cerr << "Error: Failed to parse frame fixture: " << frame_file << "\n";
        return 2;
    }

    DmaPlaneLayout layout = calculate_dma_surface_layout(fixture.width, fixture.height, fixture.format);
    DmaBufRingPool pool(slots, layout);

    /* Simulate pipeline iterations based on sequence trace */
    for (size_t i = 0; i < fixture.sequence_trace.size(); i++) {
        uint32_t slot_idx = static_cast<uint32_t>(i % pool.get_slot_count());
        uint64_t seq = fixture.sequence_trace[i];
        uint64_t ts = 1000000000ULL + i * 16666666ULL;
        uint64_t fence_id = 5000 + i;

        DmaErrorCode err = pool.v4l2_qbuf(slot_idx);
        if (err != DmaErrorCode::DMA_OK) {
            std::cerr << "Pipeline Error in v4l2_qbuf slot " << slot_idx << "\n";
            return 1;
        }

        err = pool.v4l2_dqbuf(slot_idx, seq, ts, fence_id);
        if (err != DmaErrorCode::DMA_OK) {
            std::cerr << "Pipeline Error in v4l2_dqbuf slot " << slot_idx << "\n";
            return 1;
        }

        err = pool.signal_hardware_fence(slot_idx, fence_id);
        if (err != DmaErrorCode::DMA_OK) {
            std::cerr << "Pipeline Error in signal_hardware_fence slot " << slot_idx << "\n";
            return 1;
        }

        err = pool.trt_acquire_buffer(slot_idx);
        if (err != DmaErrorCode::DMA_OK) {
            std::cerr << "Pipeline Error in trt_acquire_buffer slot " << slot_idx << "\n";
            return 1;
        }

        err = pool.trt_release_buffer(slot_idx);
        if (err != DmaErrorCode::DMA_OK) {
            std::cerr << "Pipeline Error in trt_release_buffer slot " << slot_idx << "\n";
            return 1;
        }
    }

    std::ofstream ofs;
    std::ostream *out_stream = &std::cout;
    if (!output_file.empty()) {
        ofs.open(output_file);
        if (!ofs.is_open()) {
            std::cerr << "Error: Unable to open output file: " << output_file << "\n";
            return 2;
        }
        out_stream = &ofs;
    }

    if (format == "json") {
        Exporters::export_pipeline_sim_json(pool, *out_stream);
    } else {
        Exporters::export_pipeline_sim_table(pool, *out_stream);
    }

    if (!output_file.empty()) {
        std::cout << "Pipeline results written to " << output_file << "\n";
    }

    return 0;
}

static int handle_convert_frame(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "Error: 'convert-frame' requires a frame JSON fixture.\n";
        return 2;
    }

    std::string frame_file = argv[2];
    NormalizationParams norm;
    std::string format = "table";
    std::string output_file;

    for (int i = 3; i < argc; i++) {
        if (std::strcmp(argv[i], "--full-swing") == 0) {
            norm.full_swing_bt709 = true;
        } else if (std::strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            format = argv[++i];
        } else if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        }
    }

    FrameFixture fixture;
    if (!JsonParser::parse_frame_file(frame_file, fixture)) {
        std::cerr << "Error: Failed to parse frame fixture: " << frame_file << "\n";
        return 2;
    }

    NCHWTensor tensor = ColorTensorBridge::convert_nv12_to_nchw(
        fixture.nv12_bytes.data(),
        fixture.width,
        fixture.height,
        fixture.pitch_bytes,
        norm
    );

    std::ofstream ofs;
    std::ostream *out_stream = &std::cout;
    if (!output_file.empty()) {
        ofs.open(output_file);
        if (!ofs.is_open()) {
            std::cerr << "Error: Unable to open output file: " << output_file << "\n";
            return 2;
        }
        out_stream = &ofs;
    }

    if (format == "json") {
        Exporters::export_tensor_json(tensor, *out_stream);
    } else {
        Exporters::export_tensor_table(tensor, *out_stream);
    }

    if (!output_file.empty()) {
        std::cout << "Tensor conversion written to " << output_file << "\n";
    }

    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_help(argv[0]);
        return 0;
    }

    std::string cmd = argv[1];
    if (cmd == "--version" || cmd == "-v") {
        print_version();
        return 0;
    }

    if (cmd == "--help" || cmd == "-h") {
        print_help(argv[0]);
        return 0;
    }

    if (cmd == "scan") {
        return handle_scan(argc, argv);
    }

    if (cmd == "calc-layout") {
        return handle_calc_layout(argc, argv);
    }

    if (cmd == "sim-pipeline") {
        return handle_sim_pipeline(argc, argv);
    }

    if (cmd == "convert-frame") {
        return handle_convert_frame(argc, argv);
    }

    std::cerr << "Unknown command: " << cmd << "\n\n";
    print_help(argv[0]);
    return 2;
}
