#pragma once

#include <akari/core/render_data2d.hpp>
#include <akari/image/image_rgba8.hpp>
#include <akari/vulkan/vulkan_renderer.hpp>

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace akari::vulkan_detail {

struct QueueFamilies {
    std::optional<std::uint32_t> graphics;
    std::optional<std::uint32_t> present;
};

struct VulkanContextCreateInfo {
    VulkanRendererOptions options;
    std::vector<const char*> required_instance_extensions;
    std::function<VkSurfaceKHR(VkInstance)> create_surface;
};

class VulkanContext {
public:
    explicit VulkanContext(VulkanContextCreateInfo create_info);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    [[nodiscard]] const vk::raii::Instance& instance() const noexcept { return instance_; }
    [[nodiscard]] const vk::raii::PhysicalDevice& physical_device() const noexcept { return physical_device_; }
    [[nodiscard]] const vk::raii::Device& device() const noexcept { return device_; }
    [[nodiscard]] const vk::raii::SurfaceKHR& surface() const noexcept { return surface_; }
    [[nodiscard]] const vk::raii::Queue& graphics_queue() const noexcept { return graphics_queue_; }
    [[nodiscard]] const vk::raii::Queue& present_queue() const noexcept { return present_queue_; }
    [[nodiscard]] std::uint32_t graphics_queue_family() const noexcept { return queue_families_.graphics.value(); }
    [[nodiscard]] std::uint32_t present_queue_family() const noexcept { return queue_families_.present.value_or(graphics_queue_family()); }
    [[nodiscard]] bool has_surface() const noexcept { return static_cast<bool>(*surface_); }
    [[nodiscard]] VmaAllocator allocator() const noexcept { return allocator_; }
    [[nodiscard]] std::size_t validation_error_count() const noexcept { return validation_errors_.load(); }
    [[nodiscard]] const char* device_name() const noexcept { return device_name_.c_str(); }

private:
    std::atomic_size_t validation_errors_{};
    std::string device_name_;
    bool validation_enabled_{};
    vk::raii::Context dispatcher_context_;
    vk::raii::Instance instance_{nullptr};
    vk::raii::DebugUtilsMessengerEXT debug_messenger_{nullptr};
    vk::raii::SurfaceKHR surface_{nullptr};
    vk::raii::PhysicalDevice physical_device_{nullptr};
    vk::raii::Device device_{nullptr};
    vk::raii::Queue graphics_queue_{nullptr};
    vk::raii::Queue present_queue_{nullptr};
    QueueFamilies queue_families_;
    VmaAllocator allocator_{};
};

class AllocatedBuffer {
public:
    AllocatedBuffer() = default;
    AllocatedBuffer(VmaAllocator allocator, vk::DeviceSize size, vk::BufferUsageFlags usage, bool host_visible);
    ~AllocatedBuffer();

    AllocatedBuffer(const AllocatedBuffer&) = delete;
    AllocatedBuffer& operator=(const AllocatedBuffer&) = delete;
    AllocatedBuffer(AllocatedBuffer&& other) noexcept;
    AllocatedBuffer& operator=(AllocatedBuffer&& other) noexcept;

    [[nodiscard]] vk::Buffer buffer() const noexcept { return buffer_; }
    [[nodiscard]] vk::DeviceSize size() const noexcept { return size_; }
    [[nodiscard]] void* mapped_data() const noexcept { return mapped_data_; }
    void flush(vk::DeviceSize size);
    void invalidate(vk::DeviceSize size);

private:
    void reset() noexcept;

    VmaAllocator allocator_{};
    VkBuffer buffer_{};
    VmaAllocation allocation_{};
    vk::DeviceSize size_{};
    void* mapped_data_{};
};

class AllocatedImage {
public:
    AllocatedImage() = default;
    AllocatedImage(VmaAllocator allocator, RenderExtent extent, vk::Format format, vk::ImageUsageFlags usage);
    ~AllocatedImage();

    AllocatedImage(const AllocatedImage&) = delete;
    AllocatedImage& operator=(const AllocatedImage&) = delete;
    AllocatedImage(AllocatedImage&& other) noexcept;
    AllocatedImage& operator=(AllocatedImage&& other) noexcept;

    [[nodiscard]] vk::Image image() const noexcept { return image_; }

private:
    void reset() noexcept;

    VmaAllocator allocator_{};
    VkImage image_{};
    VmaAllocation allocation_{};
};

class GeometryUpload {
public:
    explicit GeometryUpload(VmaAllocator allocator) : allocator_(allocator) {}

    void prepare(const SceneFrame2D& frame);
    void record(vk::CommandBuffer command_buffer) const;
    [[nodiscard]] vk::Buffer vertex_buffer() const noexcept { return vertices_gpu_.buffer(); }
    [[nodiscard]] vk::Buffer index_buffer() const noexcept { return indices_gpu_.buffer(); }
    [[nodiscard]] std::uint32_t index_count() const noexcept { return index_count_; }
    [[nodiscard]] vk::DeviceSize vertex_bytes() const noexcept { return vertex_bytes_; }
    [[nodiscard]] vk::DeviceSize index_bytes() const noexcept { return index_bytes_; }
    [[nodiscard]] vk::DeviceSize vertex_capacity() const noexcept { return vertices_gpu_.size(); }
    [[nodiscard]] vk::DeviceSize index_capacity() const noexcept { return indices_gpu_.size(); }
    [[nodiscard]] std::uint64_t growth_count() const noexcept { return growth_count_; }

private:
    void ensure_capacity(vk::DeviceSize vertex_bytes, vk::DeviceSize index_bytes);

    VmaAllocator allocator_{};
    AllocatedBuffer vertices_staging_;
    AllocatedBuffer vertices_gpu_;
    AllocatedBuffer indices_staging_;
    AllocatedBuffer indices_gpu_;
    vk::DeviceSize vertex_bytes_{};
    vk::DeviceSize index_bytes_{};
    std::uint32_t index_count_{};
    std::uint64_t growth_count_{};
};

struct RenderTarget2D {
    vk::Image image;
    vk::ImageView view;
    vk::Extent2D extent;
    vk::Format format;
    vk::ImageLayout initial_layout;
    vk::ImageLayout final_layout;
    glm::vec4 clear_color;
};

class SceneDrawPass2D {
public:
    SceneDrawPass2D(VulkanContext& context, const std::filesystem::path& shader_directory);

    void record(
        vk::CommandBuffer command_buffer,
        const GeometryUpload& geometry,
        const Camera2D& camera,
        const RenderTarget2D& target);
    [[nodiscard]] std::size_t pipeline_count() const noexcept { return pipelines_.size(); }

private:
    [[nodiscard]] const vk::raii::Pipeline& pipeline_for(vk::Format format);

    VulkanContext& context_;
    vk::raii::PipelineLayout pipeline_layout_{nullptr};
    std::vector<std::pair<vk::Format, vk::raii::Pipeline>> pipelines_;
    std::vector<std::uint32_t> vertex_shader_;
    std::vector<std::uint32_t> fragment_shader_;
};

struct FrameSlot {
    explicit FrameSlot(VmaAllocator allocator) : geometry(allocator) {}

    GeometryUpload geometry;
    vk::raii::Fence fence{nullptr};
    vk::raii::CommandBuffer command_buffer{nullptr};
};

class FrameScheduler {
public:
    explicit FrameScheduler(VulkanContext& context, std::size_t frame_count = 2);

    FrameSlot& begin_frame();
    void submit(
        FrameSlot& slot,
        std::span<const vk::SemaphoreSubmitInfo> waits = {},
        std::span<const vk::SemaphoreSubmitInfo> signals = {});
    void wait(FrameSlot& slot) const;
    void advance() noexcept;
    void wait_all() const;
    [[nodiscard]] std::size_t maximum_vertex_capacity() const noexcept;
    [[nodiscard]] std::size_t maximum_index_capacity() const noexcept;
    [[nodiscard]] std::uint64_t geometry_buffer_growths() const noexcept;

private:
    VulkanContext& context_;
    vk::raii::CommandPool command_pool_{nullptr};
    std::vector<FrameSlot> frames_;
    std::size_t current_frame_{};
};

void validate_scene_frame(const SceneFrame2D& frame);
[[nodiscard]] std::filesystem::path resolve_shader_directory(const VulkanRendererOptions& options);

} // namespace akari::vulkan_detail
