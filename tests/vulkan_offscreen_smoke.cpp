#include <akari/core/demo_scene.hpp>
#include <akari/core/scene_session.hpp>
#include <akari/core/tessellator2d.hpp>
#include <akari/vulkan/vulkan_offscreen_renderer.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>

namespace {

akari::SceneFrame2D evaluate(const double time)
{
    akari::SceneSession session{akari::make_unit_circle_scene()};
    akari::SceneFrame2D frame;
    akari::tessellate_2d(session.evaluate(akari::TimelineTime::from_seconds(time)), frame);
    return frame;
}

std::size_t distinct_colors(const akari::ImageRgba8& image)
{
    std::vector<std::uint32_t> colors;
    colors.reserve(image.pixels.size() / 4);
    for (std::size_t offset = 0; offset < image.pixels.size(); offset += 4) {
        colors.push_back(
            static_cast<std::uint32_t>(image.pixels[offset]) |
            (static_cast<std::uint32_t>(image.pixels[offset + 1]) << 8U) |
            (static_cast<std::uint32_t>(image.pixels[offset + 2]) << 16U) |
            (static_cast<std::uint32_t>(image.pixels[offset + 3]) << 24U));
    }
    std::ranges::sort(colors);
    return static_cast<std::size_t>(std::ranges::unique(colors).begin() - colors.begin());
}

} // namespace

int main()
{
    try {
        akari::VulkanOffscreenRenderer renderer{{.enable_validation = true}};
        if (renderer.statistics() != akari::RendererStatistics{}) {
            throw std::runtime_error("Offscreen renderer statistics were not initially zero");
        }
        const akari::OffscreenRenderRequest request{{256, 256}};
        const auto frame_zero = evaluate(0.0);
        const auto first = renderer.render(frame_zero, request);
        const auto repeated = renderer.render(frame_zero, request);
        if (first != repeated) {
            throw std::runtime_error("Repeated offscreen renders were not byte-identical");
        }
        if (distinct_colors(first) < 4) {
            throw std::runtime_error("Offscreen image does not contain the expected color variety");
        }
        const auto quarter_turn = renderer.render(evaluate(std::numbers::pi / 2.0), request);
        if (first == quarter_turn) {
            throw std::runtime_error("Moving point did not change the offscreen image");
        }

        auto large = frame_zero;
        large.vertices.resize(5000, large.vertices.front());
        (void)renderer.render(large, request);
        (void)renderer.render(large, request);
        const auto after_growth = renderer.statistics();
        (void)renderer.render(large, request);
        const auto stable = renderer.statistics();
        if (after_growth.geometry_buffer_growths == 0 ||
            stable.geometry_buffer_growths != after_growth.geometry_buffer_growths) {
            throw std::runtime_error("Geometry capacity growth statistics are incorrect");
        }
        if (stable.frames_submitted != 6 || stable.pipeline_count != 1 ||
            stable.last_vertex_bytes != large.vertices.size() * sizeof(akari::Vertex2D) ||
            stable.vertex_capacity_bytes < stable.last_vertex_bytes ||
            stable.index_capacity_bytes < stable.last_index_bytes || stable.total_upload_bytes == 0) {
            throw std::runtime_error("Offscreen renderer statistics are incomplete");
        }

        if (renderer.validation_error_count() != 0) {
            throw std::runtime_error(
                std::to_string(renderer.validation_error_count()) + " Vulkan validation errors were reported");
        }
        std::cout << "Validated deterministic offscreen rendering on " << renderer.device_name() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Offscreen Vulkan smoke test failed: " << error.what() << '\n';
        return 1;
    }
}
