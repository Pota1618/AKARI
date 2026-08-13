#include <akari/core/demo_scene.hpp>
#include <akari/core/playback_controller.hpp>
#include <akari/vulkan/vulkan_renderer.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>

namespace {

struct GlfwTerminator {
    void operator()(GLFWwindow* window) const noexcept
    {
        if (window != nullptr) {
            glfwDestroyWindow(window);
        }
    }
};

struct PreviewState {
    akari::PlaybackController* playback{};
};

void glfw_error_callback(const int code, const char* description)
{
    std::cerr << "GLFW error " << code << ": " << description << '\n';
}

void key_callback(GLFWwindow* window, const int key, int, const int action, const int mods)
{
    if (action != GLFW_PRESS && action != GLFW_REPEAT) {
        return;
    }
    auto* state = static_cast<PreviewState*>(glfwGetWindowUserPointer(window));
    if (state == nullptr || state->playback == nullptr) {
        return;
    }

    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    } else if (key == GLFW_KEY_SPACE && action == GLFW_PRESS) {
        state->playback->toggle_playing();
    } else if (key == GLFW_KEY_HOME && action == GLFW_PRESS) {
        state->playback->reset();
    } else if (key == GLFW_KEY_LEFT || key == GLFW_KEY_RIGHT) {
        const double direction = key == GLFW_KEY_RIGHT ? 1.0 : -1.0;
        const double step = (mods & GLFW_MOD_SHIFT) != 0
                                ? 1.0
                                : 1.0 / state->playback->nominal_frame_rate();
        state->playback->seek(direction * step);
    }
}

} // namespace

int main()
{
    glfwSetErrorCallback(glfw_error_callback);
    if (glfwInit() != GLFW_TRUE) {
        std::cerr << "Unable to initialize GLFW\n";
        return 1;
    }

    int result = 0;
    try {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        std::unique_ptr<GLFWwindow, GlfwTerminator> window{
            glfwCreateWindow(800, 600, "AKARI - Unit Circle", nullptr, nullptr)};
        if (!window) {
            throw std::runtime_error("Unable to create the preview window");
        }

        akari::PlaybackController playback{akari::UnitCircleScene::duration_seconds, 60.0};
        PreviewState preview_state{&playback};
        glfwSetWindowUserPointer(window.get(), &preview_state);
        glfwSetKeyCallback(window.get(), key_callback);

        akari::VulkanRenderer renderer{window.get(), {.enable_validation = true}};
        akari::UnitCircleScene scene;
        akari::SceneFrame2D frame;

        std::cout << "AKARI preview using " << renderer.device_name() << '\n';
        std::cout << "Space: play/pause, Left/Right: 1/60 s, Shift+Left/Right: 1 s, Home: reset, Esc: quit\n";

        using clock = std::chrono::steady_clock;
        auto previous = clock::now();
        auto last_title_update = previous - std::chrono::seconds{1};

        while (glfwWindowShouldClose(window.get()) == GLFW_FALSE) {
            glfwPollEvents();
            const auto now = clock::now();
            const double delta = std::chrono::duration<double>(now - previous).count();
            previous = now;
            playback.advance(delta);

            const double time = playback.time_seconds();
            const double frame_rate = playback.nominal_frame_rate();
            scene.evaluate(
                {.time_seconds = time,
                 .frame_index = static_cast<std::uint64_t>(std::floor(time * frame_rate)),
                 .frame_rate = frame_rate,
                 .random_seed = 0},
                frame);
            renderer.draw(frame);

            if (now - last_title_update >= std::chrono::milliseconds{100}) {
                std::ostringstream title;
                title << "AKARI - Unit Circle | t=" << std::fixed << std::setprecision(3) << time << " | "
                      << (playback.playing() ? "Playing" : "Paused");
                glfwSetWindowTitle(window.get(), title.str().c_str());
                last_title_update = now;
            }
        }

        renderer.wait_idle();
        if (renderer.validation_error_count() != 0) {
            std::cerr << renderer.validation_error_count() << " Vulkan validation errors were reported\n";
            result = 2;
        }
    } catch (const std::exception& error) {
        std::cerr << "AKARI preview failed: " << error.what() << '\n';
        result = 1;
    }

    glfwTerminate();
    return result;
}
