#include <akari/core/demo_scene.hpp>
#include <akari/core/error.hpp>
#include <akari/core/scene_session.hpp>
#include <akari/core/tessellator2d.hpp>
#include <akari/vulkan/vulkan_offscreen_renderer.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

akari::SceneFrame2D frame()
{
    akari::SceneSession session{akari::make_unit_circle_scene()};
    akari::SceneFrame2D result;
    akari::tessellate_2d(session.evaluate({}), result);
    return result;
}

void expect_shader_error(const std::filesystem::path& directory, const std::string_view expected_path_fragment)
{
    try {
        akari::VulkanOffscreenRenderer renderer{
            {.enable_validation = true, .shader_directory = directory}};
        (void)renderer;
        throw std::runtime_error("Expected a shader asset error");
    } catch (const akari::AkariError& error) {
        if (error.category() != akari::ErrorCategory::ShaderAsset ||
            std::string_view{error.what()}.find(expected_path_fragment) == std::string_view::npos) {
            throw;
        }
    }
}

} // namespace

int main(const int argc, char** argv)
{
    try {
        if (argc != 3) {
            throw std::invalid_argument("Usage: shader_assets <valid-shader-directory> <workspace>");
        }
        const std::filesystem::path valid_directory{argv[1]};
        const std::filesystem::path workspace{argv[2]};
        std::filesystem::create_directories(workspace);

        akari::VulkanOffscreenRenderer explicit_renderer{
            {.enable_validation = true, .shader_directory = valid_directory}};
        (void)explicit_renderer.render(frame(), {{32, 32}});

        const auto missing = workspace / "missing";
        expect_shader_error(missing, "flat_color.vert.spv");

        const auto empty = workspace / "empty";
        std::filesystem::create_directories(empty);
        std::ofstream{empty / "flat_color.vert.spv", std::ios::binary}.close();
        expect_shader_error(empty, "flat_color.vert.spv");

        const auto invalid = workspace / "invalid";
        std::filesystem::create_directories(invalid);
        const auto invalid_shader = invalid / "flat_color.vert.spv";
        std::ofstream output{invalid_shader, std::ios::binary};
        output.write("bad", 3);
        output.close();
        expect_shader_error(invalid, "flat_color.vert.spv");

        std::cout << "Runtime shader asset resolution checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Shader asset test failed: " << error.what() << '\n';
        return 1;
    }
}
