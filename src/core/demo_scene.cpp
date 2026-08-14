#include <akari/core/demo_scene.hpp>

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace akari {
namespace {

constexpr glm::vec4 axis_color{0.42F, 0.47F, 0.56F, 1.0F};
constexpr glm::vec4 circle_color{0.24F, 0.72F, 1.0F, 1.0F};
constexpr glm::vec4 point_color{1.0F, 0.36F, 0.28F, 1.0F};
constexpr glm::vec4 anchor_color{0.30F, 0.82F, 1.0F, 1.0F};
constexpr glm::vec4 spring_color{0.66F, 0.72F, 0.82F, 1.0F};
constexpr glm::vec4 mass_color{1.0F, 0.52F, 0.22F, 1.0F};

SceneNode2D node(
    const SceneId& scene,
    const LayerId layer,
    std::string stable_key,
    std::string name,
    Primitive2D primitive,
    const glm::vec4 color,
    const std::int32_t order)
{
    SceneNode2D result;
    result.id = make_node_id(scene, stable_key);
    result.stable_key = std::move(stable_key);
    result.name = std::move(name);
    result.layer = layer;
    result.primitive = std::move(primitive);
    result.color = color;
    result.draw_order = order;
    return result;
}

} // namespace

SceneDefinition make_unit_circle_scene()
{
    const SceneId scene_id{"unit-circle"};
    SceneDefinition scene{scene_id, "Unit Circle", TimelineTime::from_seconds(unit_circle_duration_seconds)};
    const LayerId layer_id = make_layer_id(scene_id, "main-canvas");
    Canvas2D canvas{layer_id, "main-canvas"};
    canvas.add_node(node(
        scene_id, layer_id, "axis-x", "X Axis", Line2D{{-2.0F, 0.0F}, {2.0F, 0.0F}, 0.02F}, axis_color, 0));
    canvas.add_node(node(
        scene_id, layer_id, "axis-y", "Y Axis", Line2D{{0.0F, -2.0F}, {0.0F, 2.0F}, 0.02F}, axis_color, 1));
    canvas.add_node(node(
        scene_id,
        layer_id,
        "circle",
        "Unit Circle",
        Circle2D{1.0F, 0.02F, unit_circle_segments},
        circle_color,
        2));
    auto point = node(
        scene_id, layer_id, "moving-point", "Moving Point", Disc2D{0.06F, 32}, point_color, 3);
    point.geometry_hint = GeometryHint::DynamicGeometry;
    canvas.add_node(point);
    scene.add_layer(std::move(canvas));

    auto& track = scene.timeline().add_track(unit_circle_point_position(scene));
    for (std::size_t index = 0; index <= unit_circle_segments; ++index) {
        const double angle = 2.0 * std::numbers::pi * static_cast<double>(index) /
                             static_cast<double>(unit_circle_segments);
        track.add_keyframe({
            TimelineTime::from_seconds(angle),
            {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))},
            Interpolation::Linear});
    }
    scene.validate();
    return scene;
}

SceneDefinition make_spring_mass_scene()
{
    const SceneId scene_id{"spring-mass"};
    SceneDefinition scene{scene_id, "Spring Mass", TimelineTime::from_seconds(spring_mass_duration_seconds)};
    const LayerId layer_id = make_layer_id(scene_id, "main-canvas");
    Canvas2D canvas{layer_id, "main-canvas"};

    auto spring = node(
        scene_id,
        layer_id,
        "spring",
        "Spring",
        Line2D{{-0.8F, 0.0F}, {1.0F, 0.0F}, 0.025F},
        spring_color,
        0);
    spring.geometry_hint = GeometryHint::DynamicGeometry;
    canvas.add_node(spring);

    auto anchor = node(
        scene_id, layer_id, "anchor", "Draggable Anchor", Disc2D{0.085F, 32}, anchor_color, 1);
    anchor.transform.translation = {-0.8F, 0.0F};
    anchor.pickable = true;
    canvas.add_node(anchor);

    auto mass = node(scene_id, layer_id, "mass", "Mass", Disc2D{0.14F, 40}, mass_color, 2);
    mass.transform.translation = {1.0F, 0.0F};
    mass.geometry_hint = GeometryHint::DynamicGeometry;
    canvas.add_node(mass);
    scene.add_layer(std::move(canvas));

    const auto anchor_property = spring_anchor_position(scene);
    scene.timeline().add_track(anchor_property);
    (void)scene.add_input_source("pointer", "Pointer", InputSourcePolicy::RecordableEvents);
    scene.add_interaction_binding(DragBinding2D{anchor_property.node, anchor_property});
    scene.add_simulation(SpringMassSystemDefinition{
        anchor_property,
        {make_node_id(scene_id, "mass"), PropertyKind::Translation2D},
        make_node_id(scene_id, "spring"),
        {1.0F, 0.0F},
        {},
        1.0F,
        12.0F,
        1.5F,
        120});
    scene.validate();
    return scene;
}

PropertyHandle<glm::vec2> unit_circle_point_position(const SceneDefinition& scene)
{
    return {make_node_id(scene.id(), "moving-point"), PropertyKind::Translation2D};
}

PropertyHandle<glm::vec2> spring_anchor_position(const SceneDefinition& scene)
{
    return {make_node_id(scene.id(), "anchor"), PropertyKind::Translation2D};
}

InputSourceId spring_pointer_source(const SceneDefinition& scene)
{
    const auto* source = scene.find_input_source("pointer");
    if (source == nullptr) {
        throw std::logic_error("The spring-mass scene does not define its pointer input source");
    }
    return source->id;
}

} // namespace akari
