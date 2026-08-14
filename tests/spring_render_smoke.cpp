#include <akari/core/demo_scene.hpp>
#include <akari/core/scene_session.hpp>
#include <akari/core/tessellator2d.hpp>
#include <akari/vulkan/vulkan_offscreen_renderer.hpp>

#include <exception>
#include <iostream>
#include <stdexcept>

namespace {

akari::SceneFrame2D frame(akari::SceneSession& session, const double seconds)
{
    akari::SceneFrame2D result;
    akari::tessellate_2d(session.evaluate(akari::TimelineTime::from_seconds(seconds)), result);
    return result;
}

} // namespace

int main()
{
    try {
        auto definition = akari::make_spring_mass_scene();
        const auto anchor = akari::spring_anchor_position(definition);
        const auto pointer = akari::spring_pointer_source(definition);
        akari::SceneSession session{std::move(definition)};
        akari::VulkanOffscreenRenderer renderer{{.enable_validation = true}};
        const akari::OffscreenRenderRequest request{{256, 256}};

        const auto original = renderer.render(frame(session, 1.5), request);
        session.begin_recording(anchor, akari::TimelineTime::from_seconds(0.5), pointer);
        session.apply_event({
            akari::TimelineTime::from_seconds(0.75),
            anchor.node,
            1,
            pointer,
            akari::PointerEvent2D{akari::PointerEventType::Drag, {0.4F, 0.5F}}});
        session.finalize_recording();
        const auto recorded = renderer.render(frame(session, 1.5), request);
        const auto repeated = renderer.render(frame(session, 1.5), request);
        if (original == recorded) {
            throw std::runtime_error("Recorded spring take did not change the rendered image");
        }
        if (recorded != repeated) {
            throw std::runtime_error("Spring scene reset/replay was not byte-identical");
        }
        session.select_previous_take(anchor);
        const auto restored = renderer.render(frame(session, 1.5), request);
        if (restored != original) {
            throw std::runtime_error("Selecting the original take did not restore the rendered scene");
        }
        if (renderer.validation_error_count() != 0) {
            throw std::runtime_error("Vulkan validation errors were reported during spring rendering");
        }
        std::cout << "Validated spring recording and deterministic offscreen replay on " << renderer.device_name() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Spring render smoke test failed: " << error.what() << '\n';
        return 1;
    }
}
