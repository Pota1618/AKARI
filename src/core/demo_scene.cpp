#include <akari/core/demo_scene.hpp>

#include <glm/geometric.hpp>

#include <array>
#include <cmath>
#include <numbers>

namespace akari {
namespace {

constexpr glm::vec4 background_axis_color{0.42F, 0.47F, 0.56F, 1.0F};
constexpr glm::vec4 circle_color{0.24F, 0.72F, 1.0F, 1.0F};
constexpr glm::vec4 point_color{1.0F, 0.36F, 0.28F, 1.0F};
constexpr float axis_half_length = 2.0F;
constexpr float axis_width = 0.02F;
constexpr float circle_width = 0.02F;
constexpr float point_radius = 0.06F;
constexpr std::size_t point_segments = 32;

void append_quad(
    SceneFrame2D& output,
    const glm::vec2 a,
    const glm::vec2 b,
    const glm::vec2 c,
    const glm::vec2 d,
    const glm::vec4 color)
{
    const auto base = static_cast<std::uint32_t>(output.vertices.size());
    output.vertices.push_back({a, color});
    output.vertices.push_back({b, color});
    output.vertices.push_back({c, color});
    output.vertices.push_back({d, color});
    output.indices.insert(output.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

void append_segment(
    SceneFrame2D& output,
    const glm::vec2 start,
    const glm::vec2 end,
    const float width,
    const glm::vec4 color)
{
    const glm::vec2 direction = end - start;
    const glm::vec2 normal = glm::normalize(glm::vec2{-direction.y, direction.x}) * (width * 0.5F);
    append_quad(output, start + normal, end + normal, end - normal, start - normal, color);
}

void append_disc(
    SceneFrame2D& output,
    const glm::vec2 center,
    const float radius,
    const std::size_t segments,
    const glm::vec4 color)
{
    const auto center_index = static_cast<std::uint32_t>(output.vertices.size());
    output.vertices.push_back({center, color});
    for (std::size_t index = 0; index < segments; ++index) {
        const float angle = static_cast<float>(2.0 * std::numbers::pi * static_cast<double>(index) /
                                               static_cast<double>(segments));
        output.vertices.push_back({center + radius * glm::vec2{std::cos(angle), std::sin(angle)}, color});
    }
    for (std::size_t index = 0; index < segments; ++index) {
        const auto current = center_index + 1 + static_cast<std::uint32_t>(index);
        const auto next = center_index + 1 + static_cast<std::uint32_t>((index + 1) % segments);
        output.indices.insert(output.indices.end(), {center_index, current, next});
    }
}

} // namespace

void UnitCircleScene::evaluate(const EvaluationContext& context, SceneFrame2D& output) const
{
    output.vertices.clear();
    output.indices.clear();
    output.vertices.reserve(4 + 4 + circle_segments * 4 + point_segments + 1);
    output.indices.reserve(12 + circle_segments * 6 + point_segments * 3);

    append_segment(
        output,
        {-axis_half_length, 0.0F},
        {axis_half_length, 0.0F},
        axis_width,
        background_axis_color);
    append_segment(
        output,
        {0.0F, -axis_half_length},
        {0.0F, axis_half_length},
        axis_width,
        background_axis_color);

    for (std::size_t index = 0; index < circle_segments; ++index) {
        const double first_angle = 2.0 * std::numbers::pi * static_cast<double>(index) /
                                   static_cast<double>(circle_segments);
        const double second_angle = 2.0 * std::numbers::pi * static_cast<double>(index + 1) /
                                    static_cast<double>(circle_segments);
        append_segment(
            output,
            {static_cast<float>(std::cos(first_angle)), static_cast<float>(std::sin(first_angle))},
            {static_cast<float>(std::cos(second_angle)), static_cast<float>(std::sin(second_angle))},
            circle_width,
            circle_color);
    }

    const float angle = static_cast<float>(context.time_seconds);
    append_disc(output, {std::cos(angle), std::sin(angle)}, point_radius, point_segments, point_color);
}

} // namespace akari
