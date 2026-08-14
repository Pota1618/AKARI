#include <akari/core/demo_scene.hpp>
#include <akari/core/error.hpp>
#include <akari/core/scene_session.hpp>
#include <akari/core/tessellator2d.hpp>
#include <akari/image/png_writer.hpp>
#include <akari/vulkan/vulkan_offscreen_renderer.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct CaptureOptions {
    std::filesystem::path output;
    double time{};
    std::uint32_t width{800};
    std::uint32_t height{600};
};

std::uint32_t parse_dimension(const std::string_view value, const std::string_view option)
{
    std::uint32_t result{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size() || result == 0) {
        throw std::invalid_argument(std::string{option} + " requires a positive 32-bit integer");
    }
    return result;
}

double parse_time(const std::string& value)
{
    std::size_t consumed{};
    const auto result = std::stod(value, &consumed);
    if (consumed != value.size() || !std::isfinite(result)) {
        throw std::invalid_argument("--time requires a finite number");
    }
    return result;
}

CaptureOptions parse_arguments(const int argc, char** argv)
{
    CaptureOptions result;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option{argv[index]};
        if (index + 1 >= argc) {
            throw std::invalid_argument(std::string{option} + " requires a value");
        }
        const std::string value{argv[++index]};
        if (option == "--output") {
            result.output = value;
        } else if (option == "--time") {
            result.time = parse_time(value);
        } else if (option == "--width") {
            result.width = parse_dimension(value, option);
        } else if (option == "--height") {
            result.height = parse_dimension(value, option);
        } else {
            throw std::invalid_argument("Unknown option: " + std::string{option});
        }
    }
    if (result.output.empty()) {
        throw std::invalid_argument("--output <path.png> is required");
    }
    result.time = std::clamp(result.time, 0.0, 2.0 * std::numbers::pi);
    return result;
}

} // namespace

int main(const int argc, char** argv)
{
    try {
        const auto options = parse_arguments(argc, argv);
        akari::SceneSession session{akari::make_unit_circle_scene()};
        akari::SceneFrame2D frame;
        akari::tessellate_2d(session.evaluate(akari::TimelineTime::from_seconds(options.time)), frame);
        akari::VulkanOffscreenRenderer renderer;
        const auto image = renderer.render(frame, {{options.width, options.height}});
        if (renderer.validation_error_count() != 0) {
            throw akari::AkariError{
                akari::ErrorCategory::RenderSubmission,
                std::to_string(renderer.validation_error_count()) + " Vulkan validation errors were reported"};
        }
        akari::write_png(options.output, image);
        std::cout << "Wrote " << options.width << 'x' << options.height << " PNG at t=" << options.time
                  << " using " << renderer.device_name() << ": " << options.output.string() << '\n';
        return 0;
    } catch (const akari::AkariError& error) {
        std::cerr << "akari_capture: [" << akari::to_string(error.category()) << "] " << error.what() << '\n'
                  << "Usage: akari_capture --output <path.png> [--time <seconds>] [--width <pixels>] [--height <pixels>]\n";
        return 1;
    } catch (const std::invalid_argument& error) {
        std::cerr << "akari_capture: [" << akari::to_string(akari::ErrorCategory::CommandLine) << "] "
                  << error.what() << '\n'
                  << "Usage: akari_capture --output <path.png> [--time <seconds>] [--width <pixels>] [--height <pixels>]\n";
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "akari_capture: " << error.what() << '\n'
                  << "Usage: akari_capture --output <path.png> [--time <seconds>] [--width <pixels>] [--height <pixels>]\n";
        return 1;
    }
}
