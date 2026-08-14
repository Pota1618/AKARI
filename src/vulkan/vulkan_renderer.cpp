#include <akari/vulkan/vulkan_renderer.hpp>

#include <akari/core/error.hpp>

#include "vulkan_backend_internal.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace akari {
namespace {

constexpr std::size_t frames_in_flight = 2;

vulkan_detail::VulkanContextCreateInfo context_create_info(
    GLFWwindow* window,
    const VulkanRendererOptions options)
{
    if (window == nullptr) {
        throw std::invalid_argument("VulkanRenderer requires a GLFW window");
    }
    if (glfwVulkanSupported() != GLFW_TRUE) {
        throw AkariError{ErrorCategory::VulkanCapability, "GLFW reports that Vulkan is unavailable"};
    }
    std::uint32_t extension_count{};
    const auto extensions = glfwGetRequiredInstanceExtensions(&extension_count);
    if (extensions == nullptr || extension_count == 0) {
        throw AkariError{
            ErrorCategory::VulkanCapability,
            "GLFW did not provide Vulkan surface extensions"};
    }
    return {
        .options = options,
        .required_instance_extensions = {extensions, extensions + extension_count},
        .create_surface = [window](const VkInstance instance) {
            VkSurfaceKHR surface{};
            const auto result = glfwCreateWindowSurface(instance, window, nullptr, &surface);
            if (result != VK_SUCCESS) {
                throw AkariError{
                    ErrorCategory::VulkanCapability,
                    "glfwCreateWindowSurface failed with VkResult " + std::to_string(result)};
            }
            return surface;
        },
    };
}

vk::SurfaceFormatKHR choose_surface_format(const std::vector<vk::SurfaceFormatKHR>& formats)
{
    const auto preferred = std::ranges::find_if(formats, [](const auto& format) {
        return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
    });
    return preferred != formats.end() ? *preferred : formats.front();
}

} // namespace

class VulkanRenderer::Impl {
public:
    Impl(GLFWwindow* window, const VulkanRendererOptions options)
        : window_(window),
          context_(context_create_info(window, options)),
          draw_pass_(context_, vulkan_detail::resolve_shader_directory(options)),
          scheduler_(context_, frames_in_flight)
    {
        image_available_.reserve(frames_in_flight);
        for (std::size_t index = 0; index < frames_in_flight; ++index) {
            image_available_.emplace_back(context_.device(), vk::SemaphoreCreateInfo{});
        }
        recreate_swapchain();
    }

    ~Impl()
    {
        try {
            wait_idle();
        } catch (...) {
        }
    }

    void draw(const SceneFrame2D& frame)
    {
        auto& slot = scheduler_.begin_frame();
        slot.geometry.prepare(frame);

        vk::ResultValue<std::uint32_t> acquisition{vk::Result::eSuccess, 0};
        try {
            acquisition = swapchain_.acquireNextImage(
                std::numeric_limits<std::uint64_t>::max(), *image_available_.at(current_frame_), nullptr);
        } catch (const vk::OutOfDateKHRError&) {
            recreate_swapchain();
            return;
        }
        const auto image_index = acquisition.value;
        if (image_fences_.at(image_index)) {
            if (context_.device().waitForFences(
                    image_fences_.at(image_index), true, std::numeric_limits<std::uint64_t>::max()) !=
                vk::Result::eSuccess) {
                throw AkariError{
                    ErrorCategory::RenderSubmission,
                    "Timed out waiting for a Vulkan swapchain image"};
            }
        }
        image_fences_.at(image_index) = *slot.fence;

        vk::CommandBufferBeginInfo begin_info;
        begin_info.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        slot.command_buffer.begin(begin_info);
        draw_pass_.record(
            *slot.command_buffer,
            slot.geometry,
            frame.camera,
            {
                .image = swapchain_images_.at(image_index),
                .view = *swapchain_views_.at(image_index),
                .extent = swapchain_extent_,
                .format = swapchain_format_,
                .initial_layout = image_initialized_.at(image_index)
                                      ? vk::ImageLayout::ePresentSrcKHR
                                      : vk::ImageLayout::eUndefined,
                .final_layout = vk::ImageLayout::ePresentSrcKHR,
                .clear_color = {0.035F, 0.047F, 0.075F, 1.0F},
            });
        slot.command_buffer.end();
        image_initialized_.at(image_index) = true;

        vk::SemaphoreSubmitInfo wait_info;
        wait_info.setSemaphore(*image_available_.at(current_frame_))
            .setStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
            .setDeviceIndex(0);
        vk::SemaphoreSubmitInfo signal_info;
        signal_info.setSemaphore(*render_finished_.at(image_index))
            .setStageMask(vk::PipelineStageFlagBits2::eAllCommands)
            .setDeviceIndex(0);
        scheduler_.submit(slot, std::span{&wait_info, 1}, std::span{&signal_info, 1});
        update_statistics(slot);

        const vk::SwapchainKHR raw_swapchain = *swapchain_;
        const vk::Semaphore raw_render_finished = *render_finished_.at(image_index);
        vk::PresentInfoKHR present_info;
        present_info.setWaitSemaphores(raw_render_finished)
            .setSwapchains(raw_swapchain)
            .setImageIndices(image_index);
        bool recreate = acquisition.result == vk::Result::eSuboptimalKHR;
        try {
            recreate = recreate || context_.present_queue().presentKHR(present_info) == vk::Result::eSuboptimalKHR;
        } catch (const vk::OutOfDateKHRError&) {
            recreate = true;
        }

        current_frame_ = (current_frame_ + 1) % frames_in_flight;
        scheduler_.advance();
        if (recreate) {
            recreate_swapchain();
        }
    }

    void wait_idle()
    {
        scheduler_.wait_all();
        context_.present_queue().waitIdle();
    }

    [[nodiscard]] std::size_t validation_error_count() const noexcept
    {
        return context_.validation_error_count();
    }

    [[nodiscard]] const char* device_name() const noexcept { return context_.device_name(); }
    [[nodiscard]] RendererStatistics statistics() const noexcept { return statistics_; }

private:
    void update_statistics(const vulkan_detail::FrameSlot& slot) noexcept
    {
        ++statistics_.frames_submitted;
        statistics_.last_vertex_bytes = static_cast<std::size_t>(slot.geometry.vertex_bytes());
        statistics_.last_index_bytes = static_cast<std::size_t>(slot.geometry.index_bytes());
        statistics_.total_upload_bytes += statistics_.last_vertex_bytes + statistics_.last_index_bytes;
        statistics_.vertex_capacity_bytes = scheduler_.maximum_vertex_capacity();
        statistics_.index_capacity_bytes = scheduler_.maximum_index_capacity();
        statistics_.geometry_buffer_growths = scheduler_.geometry_buffer_growths();
        statistics_.pipeline_count = draw_pass_.pipeline_count();
    }

    vk::Extent2D choose_extent(const vk::SurfaceCapabilitiesKHR& capabilities) const
    {
        if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
            return capabilities.currentExtent;
        }
        int width{};
        int height{};
        glfwGetFramebufferSize(window_, &width, &height);
        return {
            std::clamp(static_cast<std::uint32_t>(width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
            std::clamp(static_cast<std::uint32_t>(height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
        };
    }

    void recreate_swapchain()
    {
        int width{};
        int height{};
        glfwGetFramebufferSize(window_, &width, &height);
        while (width == 0 || height == 0) {
            glfwWaitEvents();
            glfwGetFramebufferSize(window_, &width, &height);
        }

        scheduler_.wait_all();
        if (*swapchain_) {
            context_.present_queue().waitIdle();
        }

        const auto capabilities = context_.physical_device().getSurfaceCapabilitiesKHR(*context_.surface());
        const auto formats = context_.physical_device().getSurfaceFormatsKHR(*context_.surface());
        const auto present_modes = context_.physical_device().getSurfacePresentModesKHR(*context_.surface());
        if (formats.empty() || present_modes.empty()) {
            throw AkariError{
                ErrorCategory::VulkanCapability,
                "Vulkan surface has no usable formats or present modes on " +
                    std::string{context_.device_name()}};
        }
        const auto surface_format = choose_surface_format(formats);
        const auto extent = choose_extent(capabilities);
        auto image_count = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0) {
            image_count = std::min(image_count, capabilities.maxImageCount);
        }

        vk::SwapchainCreateInfoKHR create_info;
        create_info.setSurface(*context_.surface())
            .setMinImageCount(image_count)
            .setImageFormat(surface_format.format)
            .setImageColorSpace(surface_format.colorSpace)
            .setImageExtent(extent)
            .setImageArrayLayers(1)
            .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
            .setPreTransform(capabilities.currentTransform)
            .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
            .setPresentMode(vk::PresentModeKHR::eFifo)
            .setClipped(true);
        const std::array queue_families{
            context_.graphics_queue_family(),
            context_.present_queue_family(),
        };
        if (queue_families[0] != queue_families[1]) {
            create_info.setImageSharingMode(vk::SharingMode::eConcurrent).setQueueFamilyIndices(queue_families);
        } else {
            create_info.setImageSharingMode(vk::SharingMode::eExclusive);
        }

        auto old_swapchain = std::move(swapchain_);
        if (*old_swapchain) {
            create_info.setOldSwapchain(*old_swapchain);
        }
        auto new_swapchain = vk::raii::SwapchainKHR{context_.device(), create_info};
        auto new_images = new_swapchain.getImages();
        std::vector<vk::raii::ImageView> new_views;
        std::vector<vk::raii::Semaphore> new_render_finished;
        new_views.reserve(new_images.size());
        new_render_finished.reserve(new_images.size());
        for (const auto image : new_images) {
            vk::ImageViewCreateInfo view_info;
            view_info.setImage(image)
                .setViewType(vk::ImageViewType::e2D)
                .setFormat(surface_format.format)
                .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
            new_views.emplace_back(context_.device(), view_info);
            new_render_finished.emplace_back(context_.device(), vk::SemaphoreCreateInfo{});
        }

        swapchain_views_.clear();
        render_finished_.clear();
        swapchain_ = std::move(new_swapchain);
        swapchain_images_ = std::move(new_images);
        swapchain_views_ = std::move(new_views);
        render_finished_ = std::move(new_render_finished);
        image_fences_.assign(swapchain_images_.size(), vk::Fence{});
        image_initialized_.assign(swapchain_images_.size(), false);
        swapchain_extent_ = extent;
        swapchain_format_ = surface_format.format;
    }

    GLFWwindow* window_{};
    vulkan_detail::VulkanContext context_;
    vulkan_detail::SceneDrawPass2D draw_pass_;
    vulkan_detail::FrameScheduler scheduler_;
    std::vector<vk::raii::Semaphore> image_available_;
    vk::raii::SwapchainKHR swapchain_{nullptr};
    std::vector<vk::Image> swapchain_images_;
    std::vector<vk::raii::ImageView> swapchain_views_;
    std::vector<vk::raii::Semaphore> render_finished_;
    std::vector<vk::Fence> image_fences_;
    std::vector<bool> image_initialized_;
    vk::Extent2D swapchain_extent_{};
    vk::Format swapchain_format_{vk::Format::eUndefined};
    std::size_t current_frame_{};
    RendererStatistics statistics_{};
};

VulkanRenderer::VulkanRenderer(GLFWwindow* window, const VulkanRendererOptions options)
    : impl_(std::make_unique<Impl>(window, options))
{
}

VulkanRenderer::~VulkanRenderer() = default;
VulkanRenderer::VulkanRenderer(VulkanRenderer&&) noexcept = default;
VulkanRenderer& VulkanRenderer::operator=(VulkanRenderer&&) noexcept = default;

void VulkanRenderer::draw(const SceneFrame2D& frame) { impl_->draw(frame); }
void VulkanRenderer::wait_idle() { impl_->wait_idle(); }

std::size_t VulkanRenderer::validation_error_count() const noexcept
{
    return impl_->validation_error_count();
}

const char* VulkanRenderer::device_name() const noexcept { return impl_->device_name(); }

RendererStatistics VulkanRenderer::statistics() const noexcept { return impl_->statistics(); }

} // namespace akari
