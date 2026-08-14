#pragma once

#include <akari/core/render_data2d.hpp>
#include <akari/image/image_rgba8.hpp>
#include <akari/vulkan/vulkan_renderer.hpp>

#include <glm/vec4.hpp>

#include <cstddef>
#include <memory>

namespace akari {

struct OffscreenRenderRequest {
    RenderExtent extent;
    glm::vec4 clear_color{0.035F, 0.047F, 0.075F, 1.0F};
};

class VulkanOffscreenRenderer {
public:
    explicit VulkanOffscreenRenderer(VulkanRendererOptions options = {});
    ~VulkanOffscreenRenderer();

    VulkanOffscreenRenderer(const VulkanOffscreenRenderer&) = delete;
    VulkanOffscreenRenderer& operator=(const VulkanOffscreenRenderer&) = delete;
    VulkanOffscreenRenderer(VulkanOffscreenRenderer&&) noexcept;
    VulkanOffscreenRenderer& operator=(VulkanOffscreenRenderer&&) noexcept;

    [[nodiscard]] ImageRgba8 render(const SceneFrame2D& frame, const OffscreenRenderRequest& request);

    [[nodiscard]] std::size_t validation_error_count() const noexcept;
    [[nodiscard]] const char* device_name() const noexcept;
    [[nodiscard]] RendererStatistics statistics() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace akari
