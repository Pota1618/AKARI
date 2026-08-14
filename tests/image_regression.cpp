#include "support/image_compare.hpp"

#include <akari/core/demo_scene.hpp>
#include <akari/core/scene_session.hpp>
#include <akari/core/tessellator2d.hpp>
#include <akari/image/png_writer.hpp>
#include <akari/vulkan/vulkan_offscreen_renderer.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct Options {
    std::filesystem::path golden;
    std::filesystem::path artifact_directory;
    bool update{};
};

Options parse_options(const int argc, char** argv)
{
    Options result;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option{argv[index]};
        if (index + 1 >= argc) {
            throw std::invalid_argument(std::string{option} + " requires a path");
        }
        const std::filesystem::path value{argv[++index]};
        if (option == "--golden") {
            result.golden = value;
        } else if (option == "--artifacts") {
            result.artifact_directory = value;
        } else if (option == "--update-golden") {
            result.golden = value;
            result.update = true;
        } else {
            throw std::invalid_argument("Unknown option: " + std::string{option});
        }
    }
    if (result.golden.empty()) {
        throw std::invalid_argument("--golden or --update-golden is required");
    }
    return result;
}

akari::SceneFrame2D unit_circle_frame()
{
    constexpr double time = std::numbers::pi / 2.0;
    akari::SceneSession session{akari::make_unit_circle_scene()};
    akari::SceneFrame2D frame;
    akari::tessellate_2d(session.evaluate(akari::TimelineTime::from_seconds(time)), frame);
    return frame;
}

} // namespace

int main(const int argc, char** argv)
{
    try {
        const auto options = parse_options(argc, argv);
        akari::VulkanOffscreenRenderer renderer;
        const auto actual = renderer.render(unit_circle_frame(), {{256, 256}});
        if (renderer.validation_error_count() != 0) {
            throw std::runtime_error("Vulkan validation errors were reported during image regression");
        }
        if (options.update) {
            std::filesystem::create_directories(options.golden.parent_path());
            akari::write_png(options.golden, actual);
            std::cout << "Updated golden image: " << options.golden.string() << '\n';
            return 0;
        }

        const auto expected = akari::test_support::load_png(options.golden);
        const auto comparison = akari::test_support::compare_images(expected, actual);
        if (!comparison.passes()) {
            if (!options.artifact_directory.empty()) {
                std::filesystem::create_directories(options.artifact_directory);
                akari::write_png(options.artifact_directory / "unit_circle_actual.png", actual);
                if (expected.extent == actual.extent) {
                    akari::write_png(
                        options.artifact_directory / "unit_circle_diff.png",
                        akari::test_support::difference_image(expected, actual));
                }
            }
            std::cerr << "Image regression failed: max RGB difference="
                      << static_cast<int>(comparison.maximum_rgb_difference)
                      << ", mean RGB difference=" << comparison.mean_rgb_difference
                      << ", differing pixels=" << comparison.differing_pixel_count
                      << " (" << comparison.differing_pixel_ratio * 100.0 << "%), alpha equal="
                      << (comparison.alpha_equal ? "yes" : "no") << '\n';
            return 1;
        }
        std::cout << "Unit Circle image matches golden: max RGB difference="
                  << static_cast<int>(comparison.maximum_rgb_difference)
                  << ", mean RGB difference=" << comparison.mean_rgb_difference << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Image regression test failed: " << error.what() << '\n';
        return 1;
    }
}
