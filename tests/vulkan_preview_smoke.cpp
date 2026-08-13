#include <akari/core/demo_scene.hpp>
#include <akari/vulkan/vulkan_offscreen_renderer.hpp>
#include <akari/vulkan/vulkan_renderer.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <exception>
#include <iostream>
#include <memory>

namespace {

struct GlfwWindowDeleter {
    void operator()(GLFWwindow* window) const noexcept
    {
        if (window != nullptr) {
            glfwDestroyWindow(window);
        }
    }
};

} // namespace

int main()
{
    if (glfwInit() != GLFW_TRUE) {
        std::cerr << "Vulkan smoke test could not initialize GLFW\n";
        return 1;
    }

    int result = 0;
    try {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        std::unique_ptr<GLFWwindow, GlfwWindowDeleter> window{
            glfwCreateWindow(320, 240, "AKARI Vulkan Smoke", nullptr, nullptr)};
        if (!window) {
            throw std::runtime_error("Unable to create hidden GLFW window");
        }

        akari::VulkanRenderer renderer{window.get(), {.enable_validation = true}};
        akari::VulkanOffscreenRenderer offscreen_renderer{{.enable_validation = true}};
        akari::UnitCircleScene scene;
        akari::SceneFrame2D frame;
        constexpr std::size_t smoke_frame_count = 8;
        for (std::size_t index = 0; index < smoke_frame_count; ++index) {
            const double time = static_cast<double>(index) / 60.0;
            scene.evaluate(
                {.time_seconds = time,
                 .frame_index = index,
                 .frame_rate = 60.0,
                 .random_seed = 0},
                frame);
            renderer.draw(frame);
            if (index == 2 || index == 5) {
                const auto image = offscreen_renderer.render(frame, {{128, 96}});
                if (image.pixels.size() != 128U * 96U * 4U) {
                    throw std::runtime_error("Interleaved offscreen render returned an invalid image");
                }
            }
            if (index == 3) {
                glfwSetWindowSize(window.get(), 480, 360);
            }
            glfwPollEvents();
        }
        renderer.wait_idle();
        if (renderer.validation_error_count() != 0) {
            std::cerr << renderer.validation_error_count() << " Vulkan validation errors were reported\n";
            result = 2;
        } else if (offscreen_renderer.validation_error_count() != 0) {
            std::cerr << offscreen_renderer.validation_error_count()
                      << " offscreen Vulkan validation errors were reported\n";
            result = 2;
        } else {
            std::cout << "Rendered " << smoke_frame_count << " validated frames on " << renderer.device_name() << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "Vulkan smoke test failed: " << error.what() << '\n';
        result = 1;
    }

    glfwTerminate();
    return result;
}
