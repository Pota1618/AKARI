#pragma once

#include <akari/core/render_data2d.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

struct GLFWwindow;

namespace akari {

struct VulkanRendererOptions {
    bool enable_validation{true};
    std::filesystem::path shader_directory;
};

struct RendererStatistics {
    std::uint64_t frames_submitted{};
    std::uint64_t total_upload_bytes{};
    std::size_t last_vertex_bytes{};
    std::size_t last_index_bytes{};
    std::size_t vertex_capacity_bytes{};
    std::size_t index_capacity_bytes{};
    std::uint64_t geometry_buffer_growths{};
    std::size_t pipeline_count{};

    friend bool operator==(const RendererStatistics&, const RendererStatistics&) = default;
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
    [[nodiscard]] RendererStatistics statistics() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace akari
