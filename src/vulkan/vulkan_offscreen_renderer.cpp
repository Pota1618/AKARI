#include <akari/vulkan/vulkan_offscreen_renderer.hpp>

#include "vulkan_backend_internal.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace akari {

class VulkanOffscreenRenderer::Impl {
public:
    explicit Impl(const VulkanRendererOptions options)
        : context_({.options = options}), draw_pass_(context_), scheduler_(context_)
    {
        const auto features = context_.physical_device().getFormatProperties(format_).optimalTilingFeatures;
        const auto required = vk::FormatFeatureFlagBits::eColorAttachment | vk::FormatFeatureFlagBits::eTransferSrc;
        if ((features & required) != required) {
            throw std::runtime_error(
                "GPU does not support VK_FORMAT_R8G8B8A8_SRGB as a color attachment and transfer source");
        }
    }

    ImageRgba8 render(const SceneFrame2D& frame, const OffscreenRenderRequest& request)
    {
        const auto byte_count = checked_rgba_byte_size(request.extent);
        if (!std::isfinite(request.clear_color.r) || !std::isfinite(request.clear_color.g) ||
            !std::isfinite(request.clear_color.b) || !std::isfinite(request.clear_color.a)) {
            throw std::invalid_argument("Offscreen clear color must contain finite values");
        }
        ensure_target(request.extent, byte_count);

        auto& slot = scheduler_.begin_frame();
        slot.geometry.prepare(frame);
        vk::CommandBufferBeginInfo begin_info;
        begin_info.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        slot.command_buffer.begin(begin_info);
        draw_pass_.record(
            *slot.command_buffer,
            slot.geometry,
            {
                .image = color_image_.image(),
                .view = *color_view_,
                .extent = {request.extent.width, request.extent.height},
                .format = format_,
                .initial_layout = initialized_ ? vk::ImageLayout::eTransferSrcOptimal : vk::ImageLayout::eUndefined,
                .final_layout = vk::ImageLayout::eTransferSrcOptimal,
                .clear_color = request.clear_color,
            });

        vk::BufferImageCopy copy;
        copy.setBufferOffset(0)
            .setBufferRowLength(0)
            .setBufferImageHeight(0)
            .setImageSubresource({vk::ImageAspectFlagBits::eColor, 0, 0, 1})
            .setImageOffset({0, 0, 0})
            .setImageExtent({request.extent.width, request.extent.height, 1});
        slot.command_buffer.copyImageToBuffer(
            color_image_.image(), vk::ImageLayout::eTransferSrcOptimal, readback_.buffer(), copy);
        vk::BufferMemoryBarrier2 to_host;
        to_host.setSrcStageMask(vk::PipelineStageFlagBits2::eCopy)
            .setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
            .setDstStageMask(vk::PipelineStageFlagBits2::eHost)
            .setDstAccessMask(vk::AccessFlagBits2::eHostRead)
            .setBuffer(readback_.buffer())
            .setOffset(0)
            .setSize(byte_count);
        vk::DependencyInfo to_host_dependency;
        to_host_dependency.setBufferMemoryBarriers(to_host);
        slot.command_buffer.pipelineBarrier2(to_host_dependency);
        slot.command_buffer.end();

        scheduler_.submit(slot);
        scheduler_.wait(slot);
        readback_.invalidate(byte_count);
        ImageRgba8 image{request.extent, std::vector<std::uint8_t>(byte_count)};
        std::memcpy(image.pixels.data(), readback_.mapped_data(), byte_count);
        initialized_ = true;
        scheduler_.advance();
        return image;
    }

    [[nodiscard]] std::size_t validation_error_count() const noexcept
    {
        return context_.validation_error_count();
    }

    [[nodiscard]] const char* device_name() const noexcept { return context_.device_name(); }

private:
    void ensure_target(const RenderExtent extent, const std::size_t byte_count)
    {
        if (extent == extent_) {
            return;
        }
        scheduler_.wait_all();
        color_view_ = nullptr;
        color_image_ = {};
        readback_ = {};
        color_image_ = vulkan_detail::AllocatedImage{
            context_.allocator(),
            extent,
            format_,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc};
        vk::ImageViewCreateInfo view_info;
        view_info.setImage(color_image_.image())
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(format_)
            .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
        color_view_ = vk::raii::ImageView{context_.device(), view_info};
        readback_ = vulkan_detail::AllocatedBuffer{
            context_.allocator(), byte_count, vk::BufferUsageFlagBits::eTransferDst, true};
        extent_ = extent;
        initialized_ = false;
    }

    vulkan_detail::VulkanContext context_;
    vulkan_detail::SceneDrawPass2D draw_pass_;
    vulkan_detail::FrameScheduler scheduler_;
    vulkan_detail::AllocatedImage color_image_;
    vk::raii::ImageView color_view_{nullptr};
    vulkan_detail::AllocatedBuffer readback_;
    RenderExtent extent_{};
    vk::Format format_{vk::Format::eR8G8B8A8Srgb};
    bool initialized_{};
};

VulkanOffscreenRenderer::VulkanOffscreenRenderer(const VulkanRendererOptions options)
    : impl_(std::make_unique<Impl>(options))
{
}

VulkanOffscreenRenderer::~VulkanOffscreenRenderer() = default;
VulkanOffscreenRenderer::VulkanOffscreenRenderer(VulkanOffscreenRenderer&&) noexcept = default;
VulkanOffscreenRenderer& VulkanOffscreenRenderer::operator=(VulkanOffscreenRenderer&&) noexcept = default;

ImageRgba8 VulkanOffscreenRenderer::render(const SceneFrame2D& frame, const OffscreenRenderRequest& request)
{
    return impl_->render(frame, request);
}

std::size_t VulkanOffscreenRenderer::validation_error_count() const noexcept
{
    return impl_->validation_error_count();
}

const char* VulkanOffscreenRenderer::device_name() const noexcept
{
    return impl_->device_name();
}

} // namespace akari
