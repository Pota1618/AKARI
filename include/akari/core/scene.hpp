#pragma once

#include <akari/core/identity.hpp>
#include <akari/core/render_data2d.hpp>
#include <akari/core/timeline.hpp>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace akari {

struct Transform2D {
    glm::vec2 translation{};
    float rotation_radians{};
    glm::vec2 scale{1.0F, 1.0F};
};

struct Group2D {};

struct Line2D {
    glm::vec2 start{};
    glm::vec2 end{};
    float width{0.02F};
};

struct Polyline2D {
    std::vector<glm::vec2> points;
    float width{0.02F};
    bool closed{};
};

struct Circle2D {
    float radius{1.0F};
    float width{0.02F};
    std::size_t segments{128};
};

struct Disc2D {
    float radius{0.06F};
    std::size_t segments{32};
};

using Primitive2D = std::variant<Group2D, Line2D, Polyline2D, Circle2D, Disc2D>;

enum class InputSourcePolicy {
    Deterministic,
    RecordableEvents,
    RecordableSamples,
    LiveOnly,
};

struct InputSourceDefinition {
    InputSourceId id;
    std::string stable_key;
    std::string name;
    InputSourcePolicy policy{};
};

struct DragBinding2D {
    NodeId target;
    PropertyHandle<glm::vec2> property;
};

using InteractionBinding = std::variant<DragBinding2D>;

enum class GeometryHint {
    StaticGeometry,
    DynamicGeometry,
};

struct SceneNode2D {
    NodeId id;
    std::string stable_key;
    std::string name;
    LayerId layer;
    std::optional<NodeId> parent;
    Transform2D transform;
    Primitive2D primitive;
    glm::vec4 color{1.0F};
    float opacity{1.0F};
    std::int32_t draw_order{};
    bool visible{true};
    bool enabled{true};
    bool pickable{};
    GeometryHint geometry_hint{GeometryHint::StaticGeometry};
    std::uint64_t geometry_revision{1};
};

class Canvas2D {
public:
    Canvas2D(LayerId id, std::string stable_key, Camera2D camera = {});

    void add_node(SceneNode2D node);
    void validate() const;

    [[nodiscard]] LayerId id() const noexcept { return id_; }
    [[nodiscard]] const std::string& stable_key() const noexcept { return stable_key_; }
    [[nodiscard]] const Camera2D& camera() const noexcept { return camera_; }
    [[nodiscard]] Camera2D& camera() noexcept { return camera_; }
    [[nodiscard]] const std::vector<SceneNode2D>& nodes() const noexcept { return nodes_; }
    [[nodiscard]] std::vector<SceneNode2D>& nodes() noexcept { return nodes_; }
    [[nodiscard]] const SceneNode2D* find(NodeId id) const noexcept;
    [[nodiscard]] SceneNode2D* find(NodeId id) noexcept;

private:
    LayerId id_;
    std::string stable_key_;
    Camera2D camera_;
    std::vector<SceneNode2D> nodes_;
};

using SpatialLayerDefinition = std::variant<Canvas2D>;

struct SpringMassSystemDefinition {
    PropertyHandle<glm::vec2> anchor_position;
    PropertyHandle<glm::vec2> mass_position;
    NodeId spring_node;
    glm::vec2 initial_mass_position{1.0F, 0.0F};
    glm::vec2 initial_velocity{};
    float mass{1.0F};
    float stiffness{12.0F};
    float damping{1.5F};
    std::uint32_t tick_rate{120};
};

using SimulationSystemDefinition = std::variant<SpringMassSystemDefinition>;

class SceneDefinition {
public:
    SceneDefinition(SceneId id, std::string name, TimelineTime duration);

    void add_layer(SpatialLayerDefinition layer);
    [[nodiscard]] InputSourceId add_input_source(
        std::string stable_key, std::string name, InputSourcePolicy policy);
    void add_interaction_binding(InteractionBinding binding);
    void add_simulation(SimulationSystemDefinition simulation);
    void validate() const;

    [[nodiscard]] const SceneId& id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] TimelineTime duration() const noexcept { return duration_; }
    [[nodiscard]] const std::vector<SpatialLayerDefinition>& layers() const noexcept { return layers_; }
    [[nodiscard]] std::vector<SpatialLayerDefinition>& layers() noexcept { return layers_; }
    [[nodiscard]] const Canvas2D& canvas2d() const;
    [[nodiscard]] Canvas2D& canvas2d();
    [[nodiscard]] const std::vector<SimulationSystemDefinition>& simulations() const noexcept
    {
        return simulations_;
    }
    [[nodiscard]] const std::vector<InputSourceDefinition>& input_sources() const noexcept
    {
        return input_sources_;
    }
    [[nodiscard]] const std::vector<InteractionBinding>& interaction_bindings() const noexcept
    {
        return interaction_bindings_;
    }
    [[nodiscard]] const InputSourceDefinition* find_input_source(InputSourceId id) const noexcept;
    [[nodiscard]] const InputSourceDefinition* find_input_source(std::string_view stable_key) const noexcept;
    [[nodiscard]] Timeline& timeline() noexcept { return timeline_; }
    [[nodiscard]] const Timeline& timeline() const noexcept { return timeline_; }

private:
    SceneId id_;
    std::string name_;
    TimelineTime duration_;
    std::vector<SpatialLayerDefinition> layers_;
    std::vector<InputSourceDefinition> input_sources_;
    std::vector<InteractionBinding> interaction_bindings_;
    std::vector<SimulationSystemDefinition> simulations_;
    Timeline timeline_;
};

struct WorldTransform2D {
    glm::vec2 x_axis{1.0F, 0.0F};
    glm::vec2 y_axis{0.0F, 1.0F};
    glm::vec2 translation{};
};

struct SnapshotNode2D {
    NodeId source;
    WorldTransform2D world_transform;
    Primitive2D primitive;
    glm::vec4 color{1.0F};
    std::int32_t draw_order{};
    bool pickable{};
    GeometryHint geometry_hint{GeometryHint::StaticGeometry};
    std::uint64_t geometry_revision{};
};

struct LayerSnapshot2D {
    LayerId source;
    Camera2D camera;
    std::vector<SnapshotNode2D> nodes;
};

using SpatialLayerSnapshot = std::variant<LayerSnapshot2D>;

struct SceneSnapshot {
    TimelineTime time;
    std::vector<SpatialLayerSnapshot> layers;

    [[nodiscard]] const LayerSnapshot2D& layer2d() const;
};

enum class PointerEventType {
    BeginDrag,
    Drag,
    EndDrag,
};

struct PointerEvent2D {
    PointerEventType type{};
    glm::vec2 world_position{};
};

using SceneEventPayload = std::variant<PointerEvent2D>;

struct SceneEvent {
    TimelineTime time;
    NodeId target;
    std::uint64_t sequence{};
    InputSourceId source;
    SceneEventPayload payload;
};

[[nodiscard]] glm::vec2 transform_point(const WorldTransform2D& transform, const glm::vec2& point) noexcept;
[[nodiscard]] std::optional<NodeId> hit_test_2d(const LayerSnapshot2D& layer, const glm::vec2& world_point);
[[nodiscard]] glm::vec2 screen_to_world(
    const Camera2D& camera,
    std::uint32_t framebuffer_width,
    std::uint32_t framebuffer_height,
    const glm::vec2& screen_position);

} // namespace akari
