#include <akari/core/tessellator2d.hpp>

#include <glm/geometric.hpp>

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <type_traits>

namespace akari {
namespace {

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
    const float length = glm::length(direction);
    if (length == 0.0F) {
        return;
    }
    const glm::vec2 normal = glm::vec2{-direction.y, direction.x} / length * (width * 0.5F);
    append_quad(output, start + normal, end + normal, end - normal, start - normal, color);
}

void append_disc(
    SceneFrame2D& output,
    const SnapshotNode2D& node,
    const Disc2D& disc)
{
    const auto center_index = static_cast<std::uint32_t>(output.vertices.size());
    output.vertices.push_back({transform_point(node.world_transform, {}), node.color});
    for (std::size_t index = 0; index < disc.segments; ++index) {
        const float angle = static_cast<float>(2.0 * std::numbers::pi * static_cast<double>(index) /
                                               static_cast<double>(disc.segments));
        output.vertices.push_back({
            transform_point(
                node.world_transform,
                disc.radius * glm::vec2{std::cos(angle), std::sin(angle)}),
            node.color});
    }
    for (std::size_t index = 0; index < disc.segments; ++index) {
        const auto current = center_index + 1 + static_cast<std::uint32_t>(index);
        const auto next = center_index + 1 + static_cast<std::uint32_t>((index + 1) % disc.segments);
        output.indices.insert(output.indices.end(), {center_index, current, next});
    }
}

} // namespace

void tessellate_2d(const SceneSnapshot& snapshot, SceneFrame2D& output)
{
    const auto& layer = snapshot.layer2d();
    output.camera = layer.camera;
    output.vertices.clear();
    output.indices.clear();

    for (const auto& node : layer.nodes) {
        std::visit(
            [&](const auto& primitive) {
                using T = std::decay_t<decltype(primitive)>;
                if constexpr (std::is_same_v<T, Line2D>) {
                    append_segment(
                        output,
                        transform_point(node.world_transform, primitive.start),
                        transform_point(node.world_transform, primitive.end),
                        primitive.width,
                        node.color);
                } else if constexpr (std::is_same_v<T, Polyline2D>) {
                    for (std::size_t index = 1; index < primitive.points.size(); ++index) {
                        append_segment(
                            output,
                            transform_point(node.world_transform, primitive.points[index - 1]),
                            transform_point(node.world_transform, primitive.points[index]),
                            primitive.width,
                            node.color);
                    }
                    if (primitive.closed) {
                        append_segment(
                            output,
                            transform_point(node.world_transform, primitive.points.back()),
                            transform_point(node.world_transform, primitive.points.front()),
                            primitive.width,
                            node.color);
                    }
                } else if constexpr (std::is_same_v<T, Circle2D>) {
                    for (std::size_t index = 0; index < primitive.segments; ++index) {
                        const double first_angle = 2.0 * std::numbers::pi * static_cast<double>(index) /
                                                   static_cast<double>(primitive.segments);
                        const double second_angle = 2.0 * std::numbers::pi * static_cast<double>(index + 1) /
                                                    static_cast<double>(primitive.segments);
                        append_segment(
                            output,
                            transform_point(
                                node.world_transform,
                                primitive.radius * glm::vec2{
                                    static_cast<float>(std::cos(first_angle)),
                                    static_cast<float>(std::sin(first_angle))}),
                            transform_point(
                                node.world_transform,
                                primitive.radius * glm::vec2{
                                    static_cast<float>(std::cos(second_angle)),
                                    static_cast<float>(std::sin(second_angle))}),
                            primitive.width,
                            node.color);
                    }
                } else if constexpr (std::is_same_v<T, Disc2D>) {
                    append_disc(output, node, primitive);
                }
            },
            node.primitive);
    }
}

} // namespace akari
