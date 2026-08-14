#include <akari/core/demo_scene.hpp>
#include <akari/core/error.hpp>
#include <akari/core/playback_controller.hpp>
#include <akari/core/scene_session.hpp>
#include <akari/core/tessellator2d.hpp>
#include <akari/vulkan/vulkan_renderer.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <chrono>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct GlfwTerminator {
    void operator()(GLFWwindow* window) const noexcept
    {
        if (window != nullptr) {
            glfwDestroyWindow(window);
        }
    }
};

struct PreviewOptions {
    std::string scene{"unit-circle"};
};

PreviewOptions parse_arguments(const int argc, char** argv)
{
    PreviewOptions result;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option{argv[index]};
        if (option != "--scene" || index + 1 >= argc) {
            throw std::invalid_argument("Usage: akari_preview [--scene unit-circle|spring-mass]");
        }
        result.scene = argv[++index];
    }
    if (result.scene != "unit-circle" && result.scene != "spring-mass") {
        throw std::invalid_argument("--scene must be unit-circle or spring-mass");
    }
    return result;
}

struct PreviewState {
    akari::PlaybackController* playback{};
    akari::SceneSession* session{};
    akari::InputSourceId pointer_source;
    akari::PropertyHandle<glm::vec2> armed_property;
    std::uint64_t event_sequence{};
    std::optional<akari::NodeId> dragging;
    std::string callback_error;
};

void glfw_error_callback(const int code, const char* description)
{
    std::cerr << "GLFW error " << code << ": " << description << '\n';
}

akari::TimelineTime playhead(const PreviewState& state)
{
    return akari::TimelineTime::from_seconds(state.playback->time_seconds());
}

glm::vec2 pointer_world_position(GLFWwindow* window, const akari::Camera2D& camera)
{
    double cursor_x{};
    double cursor_y{};
    int window_width{};
    int window_height{};
    int framebuffer_width{};
    int framebuffer_height{};
    glfwGetCursorPos(window, &cursor_x, &cursor_y);
    glfwGetWindowSize(window, &window_width, &window_height);
    glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
    if (window_width <= 0 || window_height <= 0 || framebuffer_width <= 0 || framebuffer_height <= 0) {
        throw std::runtime_error("Preview pointer conversion requires a nonzero window extent");
    }
    const glm::vec2 framebuffer_position{
        static_cast<float>(cursor_x) * static_cast<float>(framebuffer_width) / static_cast<float>(window_width),
        static_cast<float>(cursor_y) * static_cast<float>(framebuffer_height) / static_cast<float>(window_height)};
    return akari::screen_to_world(
        camera,
        static_cast<std::uint32_t>(framebuffer_width),
        static_cast<std::uint32_t>(framebuffer_height),
        framebuffer_position);
}

void dispatch_pointer_event(
    PreviewState& state,
    const akari::NodeId target,
    const akari::PointerEventType type,
    const glm::vec2 world_position)
{
    state.session->apply_event({
        playhead(state),
        target,
        ++state.event_sequence,
        state.pointer_source,
        akari::PointerEvent2D{type, world_position}});
}

void key_callback(GLFWwindow* window, const int key, int, const int action, const int mods)
{
    if (action != GLFW_PRESS && action != GLFW_REPEAT) {
        return;
    }
    auto* state = static_cast<PreviewState*>(glfwGetWindowUserPointer(window));
    if (state == nullptr || state->playback == nullptr || state->session == nullptr) {
        return;
    }

    try {
        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        } else if (key == GLFW_KEY_SPACE && action == GLFW_PRESS) {
            state->playback->toggle_playing();
        } else if (key == GLFW_KEY_HOME && action == GLFW_PRESS &&
                   state->session->mode() != akari::InteractionMode::Record) {
            state->playback->reset();
        } else if ((key == GLFW_KEY_LEFT || key == GLFW_KEY_RIGHT) &&
                   state->session->mode() != akari::InteractionMode::Record) {
            const double direction = key == GLFW_KEY_RIGHT ? 1.0 : -1.0;
            const double step = (mods & GLFW_MOD_SHIFT) != 0
                                    ? 1.0
                                    : 1.0 / state->playback->nominal_frame_rate();
            state->playback->seek(direction * step);
        } else if (key == GLFW_KEY_E && action == GLFW_PRESS && state->armed_property &&
                   state->session->mode() != akari::InteractionMode::Record) {
            const auto next = state->session->mode() == akari::InteractionMode::Edit
                                  ? akari::InteractionMode::Playback
                                  : akari::InteractionMode::Edit;
            state->session->set_mode(next);
        } else if (key == GLFW_KEY_R && action == GLFW_PRESS && state->armed_property) {
            if (state->session->mode() == akari::InteractionMode::Record) {
                state->session->finalize_recording();
            } else {
                state->session->set_mode(akari::InteractionMode::Playback);
                state->session->begin_recording(state->armed_property, playhead(*state), state->pointer_source);
                if (!state->playback->playing()) {
                    state->playback->toggle_playing();
                }
            }
        } else if (key == GLFW_KEY_BACKSPACE && action == GLFW_PRESS &&
                   state->session->mode() == akari::InteractionMode::Record) {
            state->session->cancel_recording();
        } else if (key == GLFW_KEY_PAGE_UP && action == GLFW_PRESS && state->armed_property &&
                   state->session->mode() != akari::InteractionMode::Record) {
            state->session->select_previous_take(state->armed_property);
        } else if (key == GLFW_KEY_PAGE_DOWN && action == GLFW_PRESS && state->armed_property &&
                   state->session->mode() != akari::InteractionMode::Record) {
            state->session->select_next_take(state->armed_property);
        }
    } catch (const std::exception& error) {
        state->callback_error = error.what();
    }
}

void mouse_button_callback(GLFWwindow* window, const int button, const int action, int)
{
    if (button != GLFW_MOUSE_BUTTON_LEFT) {
        return;
    }
    auto* state = static_cast<PreviewState*>(glfwGetWindowUserPointer(window));
    if (state == nullptr || state->session == nullptr || !state->armed_property) {
        return;
    }
    if (state->session->mode() != akari::InteractionMode::Edit &&
        state->session->mode() != akari::InteractionMode::Record) {
        return;
    }

    try {
        const auto snapshot = state->session->evaluate(playhead(*state));
        const glm::vec2 position = pointer_world_position(window, snapshot.layer2d().camera);
        if (action == GLFW_PRESS) {
            const auto target = akari::hit_test_2d(snapshot.layer2d(), position);
            if (target == state->armed_property.node) {
                state->dragging = target;
                dispatch_pointer_event(*state, *target, akari::PointerEventType::BeginDrag, position);
            }
        } else if (action == GLFW_RELEASE && state->dragging) {
            dispatch_pointer_event(*state, *state->dragging, akari::PointerEventType::EndDrag, position);
            state->dragging.reset();
        }
    } catch (const std::exception& error) {
        state->callback_error = error.what();
    }
}

void cursor_position_callback(GLFWwindow* window, double, double)
{
    auto* state = static_cast<PreviewState*>(glfwGetWindowUserPointer(window));
    if (state == nullptr || !state->dragging) {
        return;
    }
    try {
        const auto snapshot = state->session->evaluate(playhead(*state));
        dispatch_pointer_event(
            *state,
            *state->dragging,
            akari::PointerEventType::Drag,
            pointer_world_position(window, snapshot.layer2d().camera));
    } catch (const std::exception& error) {
        state->callback_error = error.what();
    }
}

} // namespace

int main(const int argc, char** argv)
{
    glfwSetErrorCallback(glfw_error_callback);
    if (glfwInit() != GLFW_TRUE) {
        std::cerr << "Unable to initialize GLFW\n";
        return 1;
    }

    int result = 0;
    try {
        const auto options = parse_arguments(argc, argv);
        auto definition = options.scene == "spring-mass" ? akari::make_spring_mass_scene()
                                                          : akari::make_unit_circle_scene();
        const auto armed_property = options.scene == "spring-mass"
                                        ? akari::spring_anchor_position(definition)
                                        : akari::PropertyHandle<glm::vec2>{};
        const auto pointer_source = options.scene == "spring-mass"
                                        ? akari::spring_pointer_source(definition)
                                        : akari::InputSourceId{};
        akari::SceneSession session{std::move(definition)};
        akari::PlaybackController playback{session.duration().seconds(), 60.0};
        PreviewState preview_state{&playback, &session, pointer_source, armed_property};

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        std::unique_ptr<GLFWwindow, GlfwTerminator> window{
            glfwCreateWindow(800, 600, ("AKARI - " + session.definition().name()).c_str(), nullptr, nullptr)};
        if (!window) {
            throw akari::AkariError{
                akari::ErrorCategory::VulkanCapability,
                "Unable to create the preview window"};
        }
        glfwSetWindowUserPointer(window.get(), &preview_state);
        glfwSetKeyCallback(window.get(), key_callback);
        glfwSetMouseButtonCallback(window.get(), mouse_button_callback);
        glfwSetCursorPosCallback(window.get(), cursor_position_callback);

        akari::VulkanRenderer renderer{window.get(), {.enable_validation = true}};
        akari::SceneFrame2D frame;
        std::cout << "AKARI preview using " << renderer.device_name() << '\n';
        std::cout << "Space: play/pause, Left/Right: seek, Home: reset, Esc: quit\n";
        if (armed_property) {
            std::cout << "E: Edit mode, R: start/finalize Record, Backspace: cancel, "
                         "PageUp/PageDown: select take, drag anchor with mouse\n";
        }

        using clock = std::chrono::steady_clock;
        auto previous = clock::now();
        auto last_title_update = previous - std::chrono::seconds{1};
        double previous_playhead = playback.time_seconds();

        while (glfwWindowShouldClose(window.get()) == GLFW_FALSE) {
            glfwPollEvents();
            if (!preview_state.callback_error.empty()) {
                throw std::runtime_error("Preview interaction failed: " + preview_state.callback_error);
            }
            const auto now = clock::now();
            const double delta = std::chrono::duration<double>(now - previous).count();
            previous = now;
            playback.advance(delta);
            const double time = playback.time_seconds();
            if (time < previous_playhead && session.mode() == akari::InteractionMode::Record) {
                session.finalize_recording();
            }
            previous_playhead = time;

            const auto timeline_time = akari::TimelineTime::from_seconds(time);
            const auto snapshot = session.evaluate(timeline_time);
            akari::tessellate_2d(snapshot, frame);
            renderer.draw(frame);

            if (now - last_title_update >= std::chrono::milliseconds{100}) {
                const auto report = session.reproducibility({}, timeline_time);
                std::ostringstream title;
                title << "AKARI - " << session.definition().name() << " | t=" << std::fixed
                      << std::setprecision(3) << time << " | " << (playback.playing() ? "Playing" : "Paused")
                      << " | " << akari::to_string(session.mode()) << " | " << akari::to_string(report.status);
                if (armed_property) {
                    title << " | Take " << session.active_take_index(armed_property) + 1 << '/'
                          << session.take_count(armed_property);
                }
                glfwSetWindowTitle(window.get(), title.str().c_str());
                last_title_update = now;
            }
        }

        renderer.wait_idle();
        if (renderer.validation_error_count() != 0) {
            std::cerr << renderer.validation_error_count() << " Vulkan validation errors were reported\n";
            result = 2;
        }
    } catch (const akari::AkariError& error) {
        std::cerr << "AKARI preview failed [" << akari::to_string(error.category()) << "]: " << error.what() << '\n';
        result = 1;
    } catch (const std::exception& error) {
        std::cerr << "AKARI preview failed: " << error.what() << '\n';
        result = 1;
    }

    glfwTerminate();
    return result;
}
