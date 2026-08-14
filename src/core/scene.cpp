#include <akari/core/scene.hpp>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <stdexcept>
#include <unordered_map>

namespace akari {
namespace {

[[nodiscard]] bool finite(const glm::vec2 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] bool finite(const glm::vec4 value) noexcept
{
    return std::isfinite(value.r) && std::isfinite(value.g) && std::isfinite(value.b) && std::isfinite(value.a);
}

[[nodiscard]] float axis_length(const glm::vec2 axis) noexcept
{
    return std::sqrt(axis.x * axis.x + axis.y * axis.y);
}

[[nodiscard]] float distance_to_segment(const glm::vec2 point, const glm::vec2 start, const glm::vec2 end)
{
    const glm::vec2 delta = end - start;
    const float length_squared = glm::dot(delta, delta);
    if (length_squared == 0.0F) {
        return glm::length(point - start);
    }
    const float alpha = std::clamp(glm::dot(point - start, delta) / length_squared, 0.0F, 1.0F);
    return glm::length(point - (start + alpha * delta));
}

} // namespace

Canvas2D::Canvas2D(const LayerId id, std::string stable_key, const Camera2D camera)
    : id_(id), stable_key_(std::move(stable_key)), camera_(camera)
{
    if (!id_ || stable_key_.empty()) {
        throw std::invalid_argument("A Canvas2D requires a stable identity");
    }
}

void Canvas2D::add_node(SceneNode2D node)
{
    if (!node.id || node.stable_key.empty() || node.layer != id_) {
        throw std::invalid_argument("A 2D node requires a valid identity and matching layer");
    }
    for (const auto& existing : nodes_) {
        if (existing.stable_key == node.stable_key) {
            throw std::invalid_argument("Duplicate node stable key: " + node.stable_key);
        }
        if (existing.id == node.id) {
            throw std::invalid_argument(
                "NodeId hash collision between '" + existing.stable_key + "' and '" + node.stable_key + "'");
        }
    }
    nodes_.push_back(std::move(node));
}

const SceneNode2D* Canvas2D::find(const NodeId id) const noexcept
{
    const auto result = std::ranges::find(nodes_, id, &SceneNode2D::id);
    return result == nodes_.end() ? nullptr : &*result;
}

SceneNode2D* Canvas2D::find(const NodeId id) noexcept
{
    const auto result = std::ranges::find(nodes_, id, &SceneNode2D::id);
    return result == nodes_.end() ? nullptr : &*result;
}

void Canvas2D::validate() const
{
    if (!finite(camera_.center) || !std::isfinite(camera_.vertical_span) || camera_.vertical_span <= 0.0F) {
        throw std::invalid_argument("Canvas2D camera values must be finite and its span positive");
    }

    for (const auto& node : nodes_) {
        if (!finite(node.transform.translation) || !finite(node.transform.scale) ||
            !std::isfinite(node.transform.rotation_radians) || !finite(node.color) ||
            !std::isfinite(node.opacity) || node.opacity < 0.0F || node.opacity > 1.0F) {
            throw std::invalid_argument("Node values must be finite and opacity must be in [0, 1]");
        }
        if (node.layer != id_) {
            throw std::invalid_argument("A node cannot belong to a different spatial layer");
        }
        if (node.parent && find(*node.parent) == nullptr) {
            throw std::invalid_argument("A transform parent must exist in the same spatial layer");
        }
        std::visit(
            [](const auto& primitive) {
                using T = std::decay_t<decltype(primitive)>;
                if constexpr (std::is_same_v<T, Line2D>) {
                    if (!finite(primitive.start) || !finite(primitive.end) || !std::isfinite(primitive.width) ||
                        primitive.width <= 0.0F) {
                        throw std::invalid_argument("Line2D geometry is invalid");
                    }
                } else if constexpr (std::is_same_v<T, Polyline2D>) {
                    if (primitive.points.size() < 2 ||
                        !std::ranges::all_of(primitive.points, [](const glm::vec2 point) { return finite(point); }) ||
                        !std::isfinite(primitive.width) || primitive.width <= 0.0F) {
                        throw std::invalid_argument("Polyline2D geometry is invalid");
                    }
                } else if constexpr (std::is_same_v<T, Circle2D> || std::is_same_v<T, Disc2D>) {
                    if (!std::isfinite(primitive.radius) || primitive.radius <= 0.0F || primitive.segments < 3) {
                        throw std::invalid_argument("Circular primitive geometry is invalid");
                    }
                    if constexpr (std::is_same_v<T, Circle2D>) {
                        if (!std::isfinite(primitive.width) || primitive.width <= 0.0F) {
                            throw std::invalid_argument("Circle2D width is invalid");
                        }
                    }
                }
            },
            node.primitive);
    }

    std::unordered_map<std::uint64_t, int> state;
    const std::function<void(const SceneNode2D&)> visit = [&](const SceneNode2D& node) {
        int& node_state = state[node.id.value];
        if (node_state == 1) {
            throw std::invalid_argument("The Canvas2D transform hierarchy contains a cycle");
        }
        if (node_state == 2) {
            return;
        }
        node_state = 1;
        if (node.parent) {
            visit(*find(*node.parent));
        }
        node_state = 2;
    };
    for (const auto& node : nodes_) {
        visit(node);
    }
}

SceneDefinition::SceneDefinition(SceneId id, std::string name, const TimelineTime duration)
    : id_(std::move(id)), name_(std::move(name)), duration_(duration)
{
    if (id_.value.empty() || name_.empty() || duration_.nanoseconds() <= 0) {
        throw std::invalid_argument("A scene requires an identity, name, and positive duration");
    }
}

void SceneDefinition::add_layer(SpatialLayerDefinition layer)
{
    if (!layers_.empty()) {
        throw std::invalid_argument("M3 supports exactly one Canvas2D per scene");
    }
    layers_.push_back(std::move(layer));
}

InputSourceId SceneDefinition::add_input_source(
    std::string stable_key, std::string name, const InputSourcePolicy policy)
{
    if (stable_key.empty() || name.empty()) {
        throw std::invalid_argument("An input source requires a stable key and name");
    }
    const std::string identity_key = "input/" + stable_key;
    const InputSourceId id{stable_id_hash(id_.value, identity_key)};
    for (const auto& existing : input_sources_) {
        if (existing.stable_key == stable_key) {
            throw std::invalid_argument("Duplicate input source stable key: " + stable_key);
        }
        if (existing.id == id) {
            throw std::invalid_argument(
                "InputSourceId hash collision between '" + existing.stable_key + "' and '" + stable_key + "'");
        }
    }
    input_sources_.push_back({id, std::move(stable_key), std::move(name), policy});
    return id;
}

void SceneDefinition::add_interaction_binding(InteractionBinding binding)
{
    interaction_bindings_.push_back(std::move(binding));
}

const InputSourceDefinition* SceneDefinition::find_input_source(const InputSourceId id) const noexcept
{
    const auto result = std::ranges::find(input_sources_, id, &InputSourceDefinition::id);
    return result == input_sources_.end() ? nullptr : &*result;
}

const InputSourceDefinition* SceneDefinition::find_input_source(const std::string_view stable_key) const noexcept
{
    const auto result = std::ranges::find(input_sources_, stable_key, &InputSourceDefinition::stable_key);
    return result == input_sources_.end() ? nullptr : &*result;
}

void SceneDefinition::add_simulation(SimulationSystemDefinition simulation)
{
    simulations_.push_back(std::move(simulation));
}

const Canvas2D& SceneDefinition::canvas2d() const
{
    if (layers_.size() != 1 || !std::holds_alternative<Canvas2D>(layers_.front())) {
        throw std::logic_error("The scene does not contain its required Canvas2D");
    }
    return std::get<Canvas2D>(layers_.front());
}

Canvas2D& SceneDefinition::canvas2d()
{
    if (layers_.size() != 1 || !std::holds_alternative<Canvas2D>(layers_.front())) {
        throw std::logic_error("The scene does not contain its required Canvas2D");
    }
    return std::get<Canvas2D>(layers_.front());
}

void SceneDefinition::validate() const
{
    if (layers_.size() != 1 || !std::holds_alternative<Canvas2D>(layers_.front())) {
        throw std::invalid_argument("M3 scenes require exactly one Canvas2D");
    }
    canvas2d().validate();
    std::vector<NodeId> interaction_targets;
    for (const auto& binding : interaction_bindings_) {
        std::visit(
            [this, &interaction_targets](const auto& interaction) {
                using T = std::decay_t<decltype(interaction)>;
                if constexpr (std::is_same_v<T, DragBinding2D>) {
                    if (!interaction.target || interaction.property.node != interaction.target ||
                        interaction.property.kind != PropertyKind::Translation2D ||
                        canvas2d().find(interaction.target) == nullptr ||
                        timeline_.find(interaction.property) == nullptr) {
                        throw std::invalid_argument("A 2D drag binding must target a recordable translation property");
                    }
                    if (std::ranges::find(interaction_targets, interaction.target) != interaction_targets.end()) {
                        throw std::invalid_argument("A node cannot have more than one 2D drag binding");
                    }
                    interaction_targets.push_back(interaction.target);
                }
            },
            binding);
    }
    std::vector<PropertyHandle<glm::vec2>> simulation_outputs;
    for (const auto& system : simulations_) {
        std::visit(
            [this, &simulation_outputs](const auto& simulation) {
                using T = std::decay_t<decltype(simulation)>;
                if constexpr (std::is_same_v<T, SpringMassSystemDefinition>) {
                    if (simulation.anchor_position.node == simulation.mass_position.node ||
                        simulation.anchor_position.kind != PropertyKind::Translation2D ||
                        simulation.mass_position.kind != PropertyKind::Translation2D ||
                        canvas2d().find(simulation.anchor_position.node) == nullptr ||
                        canvas2d().find(simulation.mass_position.node) == nullptr ||
                        canvas2d().find(simulation.spring_node) == nullptr || simulation.mass <= 0.0F ||
                        simulation.stiffness < 0.0F || simulation.damping < 0.0F || simulation.tick_rate == 0 ||
                        !std::isfinite(simulation.mass) || !std::isfinite(simulation.stiffness) ||
                        !std::isfinite(simulation.damping) || !finite(simulation.initial_mass_position) ||
                        !finite(simulation.initial_velocity)) {
                        throw std::invalid_argument("Spring-mass simulation definition is invalid");
                    }
                    const auto* spring_node = canvas2d().find(simulation.spring_node);
                    if (!std::holds_alternative<Line2D>(spring_node->primitive)) {
                        throw std::invalid_argument("A spring-mass link must reference a Line2D node");
                    }
                    if (timeline_.find(simulation.mass_position) != nullptr) {
                        throw std::invalid_argument(
                            "A simulation output property cannot also have a timeline track");
                    }
                    if (std::ranges::find(simulation_outputs, simulation.mass_position) !=
                        simulation_outputs.end()) {
                        throw std::invalid_argument(
                            "A property cannot be produced by more than one simulation system");
                    }
                    simulation_outputs.push_back(simulation.mass_position);
                }
            },
            system);
    }
}

const LayerSnapshot2D& SceneSnapshot::layer2d() const
{
    if (layers.size() != 1 || !std::holds_alternative<LayerSnapshot2D>(layers.front())) {
        throw std::logic_error("The scene snapshot does not contain its required 2D layer");
    }
    return std::get<LayerSnapshot2D>(layers.front());
}

glm::vec2 transform_point(const WorldTransform2D& transform, const glm::vec2& point) noexcept
{
    return transform.translation + transform.x_axis * point.x + transform.y_axis * point.y;
}

std::optional<NodeId> hit_test_2d(const LayerSnapshot2D& layer, const glm::vec2& world_point)
{
    for (auto node = layer.nodes.rbegin(); node != layer.nodes.rend(); ++node) {
        if (!node->pickable) {
            continue;
        }
        const float scale = std::max(axis_length(node->world_transform.x_axis), axis_length(node->world_transform.y_axis));
        const bool hit = std::visit(
            [&](const auto& primitive) {
                using T = std::decay_t<decltype(primitive)>;
                if constexpr (std::is_same_v<T, Disc2D>) {
                    return glm::length(world_point - transform_point(node->world_transform, {})) <=
                           primitive.radius * scale;
                } else if constexpr (std::is_same_v<T, Circle2D>) {
                    const float radius = primitive.radius * scale;
                    return std::abs(glm::length(world_point - transform_point(node->world_transform, {})) - radius) <=
                           primitive.width * scale;
                } else if constexpr (std::is_same_v<T, Line2D>) {
                    return distance_to_segment(
                               world_point,
                               transform_point(node->world_transform, primitive.start),
                               transform_point(node->world_transform, primitive.end)) <=
                           primitive.width * scale;
                } else if constexpr (std::is_same_v<T, Polyline2D>) {
                    for (std::size_t index = 1; index < primitive.points.size(); ++index) {
                        if (distance_to_segment(
                                world_point,
                                transform_point(node->world_transform, primitive.points[index - 1]),
                                transform_point(node->world_transform, primitive.points[index])) <=
                            primitive.width * scale) {
                            return true;
                        }
                    }
                    if (primitive.closed &&
                        distance_to_segment(
                            world_point,
                            transform_point(node->world_transform, primitive.points.back()),
                            transform_point(node->world_transform, primitive.points.front())) <=
                            primitive.width * scale) {
                        return true;
                    }
                    return false;
                } else {
                    return false;
                }
            },
            node->primitive);
        if (hit) {
            return node->source;
        }
    }
    return std::nullopt;
}

glm::vec2 screen_to_world(
    const Camera2D& camera,
    const std::uint32_t framebuffer_width,
    const std::uint32_t framebuffer_height,
    const glm::vec2& screen_position)
{
    if (framebuffer_width == 0 || framebuffer_height == 0 || !finite(screen_position)) {
        throw std::invalid_argument("Screen-to-world conversion requires a nonzero extent and finite position");
    }
    const float half_height = camera.vertical_span * 0.5F;
    const float half_width = half_height * static_cast<float>(framebuffer_width) /
                             static_cast<float>(framebuffer_height);
    const float normalized_x = 2.0F * screen_position.x / static_cast<float>(framebuffer_width) - 1.0F;
    const float normalized_y = 1.0F - 2.0F * screen_position.y / static_cast<float>(framebuffer_height);
    return camera.center + glm::vec2{normalized_x * half_width, normalized_y * half_height};
}

} // namespace akari
