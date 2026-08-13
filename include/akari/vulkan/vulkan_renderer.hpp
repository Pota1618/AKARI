#pragma once

#include <akari/core/scene2d.hpp>

#include <cstddef>
#include <memory>

struct GLFWwindow;

namespace akari {

struct VulkanRendererOptions {
    bool enable_validation{true};
};

class VulkanRenderer {
public:
    explicit VulkanRenderer(GLFWwindow* window, VulkanRendererOptions options = {});
    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;
    VulkanRenderer(VulkanRenderer&&) noexcept;
    VulkanRenderer& operator=(VulkanRenderer&&) noexcept;

    void draw(const SceneFrame2D& frame);
    void wait_idle();

    [[nodiscard]] std::size_t validation_error_count() const noexcept;
    [[nodiscard]] const char* device_name() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace akari
