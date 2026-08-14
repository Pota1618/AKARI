#include <akari/core/scene.hpp>
#include <akari/core/demo_scene.hpp>

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

template <typename Function>
void check_throws(Function&& function, const std::string_view message)
{
    try {
        function();
        check(false, message);
    } catch (const std::exception&) {
    }
}

akari::SceneNode2D make_node(
    const akari::NodeId id,
    std::string key,
    const akari::LayerId layer,
    const std::optional<akari::NodeId> parent = {})
{
    akari::SceneNode2D result;
    result.id = id;
    result.stable_key = std::move(key);
    result.name = result.stable_key;
    result.layer = layer;
    result.parent = parent;
    result.primitive = akari::Group2D{};
    return result;
}

void test_stable_identity()
{
    const akari::SceneId scene{"identity-test"};
    check(
        akari::make_node_id(scene, "point") == akari::make_node_id(scene, "point"),
        "stable keys produce stable NodeId values");
    check(
        akari::make_node_id(scene, "point") != akari::make_node_id(scene, "other"),
        "different stable keys produce different NodeId values");

    const auto layer = akari::make_layer_id(scene, "canvas");
    akari::Canvas2D canvas{layer, "canvas"};
    canvas.add_node(make_node({123}, "first", layer));
    check_throws(
        [&] { canvas.add_node(make_node({123}, "collision", layer)); },
        "NodeId hash collisions are rejected");
    check_throws(
        [&] { canvas.add_node(make_node({456}, "first", layer)); },
        "duplicate stable keys are rejected");
}

void test_hierarchy_validation()
{
    const akari::SceneId scene{"hierarchy-test"};
    const auto layer = akari::make_layer_id(scene, "canvas");
    const auto first = akari::make_node_id(scene, "first");
    const auto second = akari::make_node_id(scene, "second");
    akari::Canvas2D cyclic{layer, "canvas"};
    cyclic.add_node(make_node(first, "first", layer, second));
    cyclic.add_node(make_node(second, "second", layer, first));
    check_throws([&] { cyclic.validate(); }, "transform hierarchy cycles are rejected");

    akari::Canvas2D missing{layer, "canvas"};
    missing.add_node(make_node(first, "first", layer, akari::NodeId{999}));
    check_throws([&] { missing.validate(); }, "missing same-layer parent is rejected");
}

void test_layer_limit()
{
    const akari::SceneId scene_id{"layer-test"};
    akari::SceneDefinition scene{scene_id, "Layer Test", akari::TimelineTime::from_seconds(1.0)};
    scene.add_layer(akari::Canvas2D{akari::make_layer_id(scene_id, "first"), "first"});
    check_throws(
        [&] { scene.add_layer(akari::Canvas2D{akari::make_layer_id(scene_id, "second"), "second"}); },
        "M3 rejects multiple spatial layers");
}

void test_single_persistent_property_source()
{
    auto scene = akari::make_spring_mass_scene();
    const auto mass = akari::make_node_id(scene.id(), "mass");
    scene.timeline().add_track(
        akari::PropertyHandle<glm::vec2>{mass, akari::PropertyKind::Translation2D});
    check_throws([&] { scene.validate(); }, "simulation output and timeline source conflict is rejected");
}

void test_input_source_identity()
{
    const akari::SceneId scene_id{"input-test"};
    akari::SceneDefinition scene{scene_id, "Input Test", akari::TimelineTime::from_seconds(1.0)};
    const auto first = scene.add_input_source("pointer", "Pointer", akari::InputSourcePolicy::RecordableEvents);
    check(static_cast<bool>(first), "input source receives a stable identity");
    check(scene.find_input_source("pointer")->id == first, "input source can be found by stable key");
    check_throws(
        [&] { (void)scene.add_input_source("pointer", "Duplicate", akari::InputSourcePolicy::LiveOnly); },
        "duplicate input source stable key is rejected");
}

} // namespace

int main()
{
    test_stable_identity();
    test_hierarchy_validation();
    test_layer_limit();
    test_single_persistent_property_source();
    test_input_source_identity();
    if (failures != 0) {
        std::cerr << failures << " scene model checks failed\n";
        return 1;
    }
    std::cout << "All scene model checks passed\n";
    return 0;
}
