#include <akari/core/demo_scene.hpp>
#include <akari/core/scene_session.hpp>
#include <akari/core/tessellator2d.hpp>

#include <cmath>
#include <exception>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

bool near(const float lhs, const float rhs, const float tolerance = 1.0e-5F)
{
    return std::abs(lhs - rhs) <= tolerance;
}

glm::vec2 node_position(const akari::SceneSnapshot& snapshot, const akari::NodeId id)
{
    for (const auto& node : snapshot.layer2d().nodes) {
        if (node.source == id) {
            return node.world_transform.translation;
        }
    }
    throw std::runtime_error("snapshot node missing");
}

const akari::SnapshotNode2D& snapshot_node(const akari::SceneSnapshot& snapshot, const akari::NodeId id)
{
    for (const auto& node : snapshot.layer2d().nodes) {
        if (node.source == id) {
            return node;
        }
    }
    throw std::runtime_error("snapshot node missing");
}

void test_scene_identity_and_hit_testing()
{
    auto definition = akari::make_spring_mass_scene();
    const auto anchor = akari::spring_anchor_position(definition);
    akari::SceneSession session{std::move(definition)};
    const auto snapshot = session.evaluate({});
    check(akari::hit_test_2d(snapshot.layer2d(), {-0.8F, 0.0F}) == anchor.node, "2D hit testing returns stable NodeId");
    const auto center = akari::screen_to_world(snapshot.layer2d().camera, 800, 600, {400.0F, 300.0F});
    check(center == snapshot.layer2d().camera.center, "screen center maps to camera center");
}

void test_recording_and_reproducibility()
{
    auto definition = akari::make_spring_mass_scene();
    const auto anchor = akari::spring_anchor_position(definition);
    const auto pointer = akari::spring_pointer_source(definition);
    akari::SceneSession session{std::move(definition)};
    const auto start = akari::TimelineTime::from_seconds(1.0);
    session.begin_recording(anchor, start, pointer);
    check(session.reproducibility({}, start).status == akari::SessionReproducibility::Recording,
          "recording status reported");
    session.apply_event({
        akari::TimelineTime::from_seconds(1.5),
        anchor.node,
        1,
        pointer,
        akari::PointerEvent2D{akari::PointerEventType::Drag, {0.25F, 0.5F}}});
    session.finalize_recording();
    check(session.reproducibility({}, start).status == akari::SessionReproducibility::Replayable,
          "finalized take becomes replayable");
    check(session.take_count(anchor) == 2, "recording creates a second take");
    check(session.definition().canvas2d().find(anchor.node)->transform.translation == glm::vec2(-0.8F, 0.0F),
          "recording does not mutate the base property");
    check(node_position(session.evaluate(akari::TimelineTime::from_seconds(1.5)), anchor.node) == glm::vec2(0.25F, 0.5F),
          "recorded curve evaluates at its keyframe");
    session.select_previous_take(anchor);
    check(node_position(session.evaluate(akari::TimelineTime::from_seconds(1.5)), anchor.node) == glm::vec2(-0.8F, 0.0F),
          "previous take restores original motion");

    session.apply_event({
        akari::TimelineTime::from_seconds(2.0),
        anchor.node,
        2,
        pointer,
        akari::PointerEvent2D{akari::PointerEventType::Drag, {0.0F, 0.0F}}});
    check(session.reproducibility({}, akari::TimelineTime::from_seconds(2.0)).status ==
              akari::SessionReproducibility::LiveOnly,
          "unrecorded recordable input marks session live-only");
    session.clear_live_overrides();
    check(session.reproducibility({}, akari::TimelineTime::from_seconds(2.0)).status ==
              akari::SessionReproducibility::Replayable,
          "canonical reset clears live-only override");
}

void test_edit_changes_only_base_property()
{
    auto definition = akari::make_spring_mass_scene();
    const auto anchor = akari::spring_anchor_position(definition);
    const auto pointer = akari::spring_pointer_source(definition);
    akari::SceneSession session{std::move(definition)};
    session.set_mode(akari::InteractionMode::Edit);
    session.apply_event({
        akari::TimelineTime::from_seconds(0.5),
        anchor.node,
        1,
        pointer,
        akari::PointerEvent2D{akari::PointerEventType::Drag, {-0.25F, 0.75F}}});
    check(session.definition().canvas2d().find(anchor.node)->transform.translation == glm::vec2(-0.25F, 0.75F),
          "edit changes the base property");
    check(session.take_count(anchor) == 1, "edit does not create a take");
}

void test_same_time_recording_sequence()
{
    auto definition = akari::make_spring_mass_scene();
    const auto anchor = akari::spring_anchor_position(definition);
    const auto pointer = akari::spring_pointer_source(definition);
    akari::SceneSession session{std::move(definition)};
    const auto time = akari::TimelineTime::from_seconds(1.0);
    session.begin_recording(anchor, time, pointer);
    session.apply_event({
        time,
        anchor.node,
        9,
        pointer,
        akari::PointerEvent2D{akari::PointerEventType::Drag, {0.5F, 0.25F}}});
    session.apply_event({
        time,
        anchor.node,
        3,
        pointer,
        akari::PointerEvent2D{akari::PointerEventType::Drag, {-0.5F, -0.25F}}});
    session.finalize_recording();
    check(node_position(session.evaluate(time), anchor.node) == glm::vec2(0.5F, 0.25F),
          "largest sequence wins for equal-time recording events");
}

void test_fixed_step_simulation()
{
    auto definition = akari::make_spring_mass_scene();
    const auto mass = akari::make_node_id(definition.id(), "mass");
    akari::SceneSession session{std::move(definition)};
    const auto time = akari::TimelineTime::from_seconds(1.25);
    const auto first = node_position(session.evaluate(time), mass);
    (void)session.evaluate(akari::TimelineTime::from_seconds(4.0));
    const auto repeated = node_position(session.evaluate(time), mass);
    check(first == repeated, "spring simulation reset/replay is seek-order independent");
    check(first != glm::vec2(1.0F, 0.0F), "spring simulation advances from initial state");

    for (const double frame_rate : {30.0, 60.0, 120.0}) {
        akari::SceneSession sampled{akari::make_spring_mass_scene()};
        const auto frames = static_cast<int>(std::floor(time.seconds() * frame_rate));
        for (int frame = 0; frame < frames; ++frame) {
            (void)sampled.evaluate(akari::TimelineTime::from_seconds(frame / frame_rate));
        }
        check(node_position(sampled.evaluate(time), mass) == first,
              "spring simulation is independent of render frame rate");
    }
}

void test_typed_property_evaluation()
{
    auto definition = akari::make_unit_circle_scene();
    const auto circle = akari::make_node_id(definition.id(), "circle");
    definition.timeline().add_track(
        akari::PropertyHandle<float>{circle, akari::PropertyKind::Rotation2D})
        .add_keyframe({{}, 1.57079632679F, akari::Interpolation::Linear});
    definition.timeline().add_track(
        akari::PropertyHandle<glm::vec2>{circle, akari::PropertyKind::Scale2D})
        .add_keyframe({{}, {2.0F, 1.0F}, akari::Interpolation::Linear});
    definition.timeline().add_track(
        akari::PropertyHandle<glm::vec4>{circle, akari::PropertyKind::Color})
        .add_keyframe({{}, {0.1F, 0.2F, 0.3F, 0.4F}, akari::Interpolation::Linear});
    definition.timeline().add_track(
        akari::PropertyHandle<float>{circle, akari::PropertyKind::Opacity})
        .add_keyframe({{}, 0.5F, akari::Interpolation::Linear});

    akari::SceneSession session{std::move(definition)};
    const auto snapshot = session.evaluate({});
    const auto& node = snapshot_node(snapshot, circle);
    check(near(node.world_transform.x_axis.x, 0.0F) && near(node.world_transform.x_axis.y, 2.0F),
          "rotation and scale tracks affect the snapshot transform");
    check(near(node.color.r, 0.1F) && near(node.color.g, 0.2F) && near(node.color.b, 0.3F) &&
              near(node.color.a, 0.2F),
          "color and opacity tracks affect the snapshot color");
}

} // namespace

int main()
{
    test_scene_identity_and_hit_testing();
    test_recording_and_reproducibility();
    test_same_time_recording_sequence();
    test_edit_changes_only_base_property();
    test_fixed_step_simulation();
    test_typed_property_evaluation();
    if (failures != 0) {
        std::cerr << failures << " scene session checks failed\n";
        return 1;
    }
    std::cout << "All scene session checks passed\n";
    return 0;
}
