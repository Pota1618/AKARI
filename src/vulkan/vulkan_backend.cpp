#define VMA_IMPLEMENTATION
#include "vulkan_backend_internal.hpp"

#include <glm/mat4x4.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace akari::vulkan_detail {
namespace {

constexpr std::array required_swapchain_extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
constexpr vk::DeviceSize minimum_geometry_capacity = 64 * 1024;

VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    const VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void* user_data)
{
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
        static_cast<std::atomic_size_t*>(user_data)->fetch_add(1);
    }
    std::cerr << "Vulkan validation: " << callback_data->pMessage << '\n';
    return VK_FALSE;
}

bool contains_layer(const std::vector<vk::LayerProperties>& layers, const char* name)
{
    return std::ranges::any_of(layers, [name](const auto& layer) { return std::strcmp(layer.layerName, name) == 0; });
}

bool contains_extension(const std::vector<vk::ExtensionProperties>& extensions, const char* name)
{
    return std::ranges::any_of(
        extensions, [name](const auto& extension) { return std::strcmp(extension.extensionName, name) == 0; });
}

QueueFamilies find_queue_families(const vk::raii::PhysicalDevice& device, const vk::SurfaceKHR surface)
{
    QueueFamilies result;
    const auto properties = device.getQueueFamilyProperties();
    for (std::uint32_t index = 0; index < properties.size(); ++index) {
        if ((properties[index].queueFlags & vk::QueueFlagBits::eGraphics) != vk::QueueFlags{}) {
            result.graphics = index;
        }
        if (surface && device.getSurfaceSupportKHR(index, surface)) {
            result.present = index;
        }
    }
    return result;
}

std::vector<std::uint32_t> read_spirv(const std::string& path)
{
    std::ifstream input{path, std::ios::ate | std::ios::binary};
    if (!input) {
        throw std::runtime_error("Unable to open SPIR-V shader asset: " + path);
    }
    const auto byte_count = input.tellg();
    if (byte_count <= 0 || byte_count % 4 != 0) {
        throw std::runtime_error("Invalid SPIR-V shader size: " + path);
    }
    std::vector<std::uint32_t> code(static_cast<std::size_t>(byte_count) / sizeof(std::uint32_t));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(code.data()), byte_count);
    if (!input) {
        throw std::runtime_error("Unable to read SPIR-V shader asset: " + path);
    }
    return code;
}

vk::DeviceSize growing_capacity(const vk::DeviceSize required)
{
    auto capacity = minimum_geometry_capacity;
    while (capacity < required) {
        if (capacity > std::numeric_limits<vk::DeviceSize>::max() / 2) {
            throw std::overflow_error("Geometry buffer capacity overflow");
        }
        capacity *= 2;
    }
    return capacity;
}

} // namespace

VulkanContext::VulkanContext(VulkanContextCreateInfo create_info)
{
    constexpr const char* validation_layer = "VK_LAYER_KHRONOS_validation";
    const auto layers = dispatcher_context_.enumerateInstanceLayerProperties();
    validation_enabled_ = create_info.options.enable_validation && contains_layer(layers, validation_layer);
    if (create_info.options.enable_validation && !validation_enabled_) {
        std::cerr << "Vulkan validation layer is unavailable; continuing without validation\n";
    }

    auto extensions = std::move(create_info.required_instance_extensions);
    if (validation_enabled_) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    std::sort(extensions.begin(), extensions.end(), [](const char* lhs, const char* rhs) {
        return std::strcmp(lhs, rhs) < 0;
    });
    extensions.erase(
        std::unique(extensions.begin(), extensions.end(), [](const char* lhs, const char* rhs) {
            return std::strcmp(lhs, rhs) == 0;
        }),
        extensions.end());

    vk::ApplicationInfo application_info;
    application_info.setPApplicationName("AKARI").setApplicationVersion(VK_MAKE_VERSION(0, 2, 0)).setApiVersion(VK_API_VERSION_1_3);
    vk::InstanceCreateInfo instance_info;
    instance_info.setPApplicationInfo(&application_info).setPEnabledExtensionNames(extensions);
    if (validation_enabled_) {
        instance_info.setPEnabledLayerNames(validation_layer);
    }
    instance_ = vk::raii::Instance{dispatcher_context_, instance_info};

    if (validation_enabled_) {
        vk::DebugUtilsMessengerCreateInfoEXT debug_info;
        debug_info.setMessageSeverity(
                      vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                      vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)
            .setMessageType(
                vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance)
            .setPfnUserCallback(debug_callback)
            .setPUserData(&validation_errors_);
        debug_messenger_ = vk::raii::DebugUtilsMessengerEXT{instance_, debug_info};
    }

    if (create_info.create_surface) {
        const auto raw_surface = create_info.create_surface(static_cast<VkInstance>(*instance_));
        if (raw_surface == VK_NULL_HANDLE) {
            throw std::runtime_error("Surface factory returned a null Vulkan surface");
        }
        surface_ = vk::raii::SurfaceKHR{instance_, raw_surface};
    }

    std::ostringstream diagnostics;
    int best_score = std::numeric_limits<int>::min();
    for (const auto& candidate : vk::raii::PhysicalDevices{instance_}) {
        const auto properties = candidate.getProperties();
        const auto families = find_queue_families(candidate, *surface_);
        std::vector<std::string> rejection_reasons;
        if (properties.apiVersion < VK_API_VERSION_1_3) {
            rejection_reasons.emplace_back("Vulkan 1.3 is unavailable");
        }
        if (!families.graphics) {
            rejection_reasons.emplace_back("graphics queue is unavailable");
        }
        if (*surface_ && !families.present) {
            rejection_reasons.emplace_back("present queue is unavailable");
        }

        const auto feature_chain =
            candidate.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features>();
        const auto& features13 = feature_chain.get<vk::PhysicalDeviceVulkan13Features>();
        if (!features13.dynamicRendering) {
            rejection_reasons.emplace_back("dynamicRendering is unavailable");
        }
        if (!features13.synchronization2) {
            rejection_reasons.emplace_back("synchronization2 is unavailable");
        }

        if (*surface_) {
            const auto device_extensions = candidate.enumerateDeviceExtensionProperties();
            for (const auto* required : required_swapchain_extensions) {
                if (!contains_extension(device_extensions, required)) {
                    rejection_reasons.emplace_back(std::string{"missing device extension "} + required);
                }
            }
        }

        if (!rejection_reasons.empty()) {
            diagnostics << "Rejected GPU " << properties.deviceName << ": ";
            for (std::size_t i = 0; i < rejection_reasons.size(); ++i) {
                diagnostics << (i == 0 ? "" : ", ") << rejection_reasons[i];
            }
            diagnostics << '\n';
            continue;
        }

        int score = 0;
        if (properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
            score = 200;
        } else if (properties.deviceType == vk::PhysicalDeviceType::eIntegratedGpu) {
            score = 100;
        }
        if (score > best_score) {
            best_score = score;
            physical_device_ = vk::raii::PhysicalDevice{candidate};
            queue_families_ = families;
            device_name_ = properties.deviceName.data();
        }
    }
    if (!*physical_device_) {
        throw std::runtime_error("No suitable Vulkan 1.3 GPU was found.\n" + diagnostics.str());
    }
    if (!diagnostics.str().empty()) {
        std::clog << diagnostics.str();
    }

    std::set<std::uint32_t> unique_families{queue_families_.graphics.value()};
    if (queue_families_.present) {
        unique_families.insert(queue_families_.present.value());
    }
    constexpr float queue_priority = 1.0F;
    std::vector<vk::DeviceQueueCreateInfo> queue_infos;
    for (const auto family : unique_families) {
        vk::DeviceQueueCreateInfo queue_info;
        queue_info.setQueueFamilyIndex(family).setQueuePriorities(queue_priority);
        queue_infos.push_back(queue_info);
    }
    vk::PhysicalDeviceVulkan13Features features13;
    features13.setDynamicRendering(true).setSynchronization2(true);
    vk::DeviceCreateInfo device_info;
    device_info.setPNext(&features13).setQueueCreateInfos(queue_infos);
    if (*surface_) {
        device_info.setPEnabledExtensionNames(required_swapchain_extensions);
    }
    device_ = vk::raii::Device{physical_device_, device_info};
    graphics_queue_ = vk::raii::Queue{device_, queue_families_.graphics.value(), 0};
    if (queue_families_.present) {
        present_queue_ = vk::raii::Queue{device_, queue_families_.present.value(), 0};
    }

    VmaAllocatorCreateInfo allocator_info{};
    allocator_info.flags = VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE4_BIT;
    allocator_info.physicalDevice = static_cast<VkPhysicalDevice>(*physical_device_);
    allocator_info.device = static_cast<VkDevice>(*device_);
    allocator_info.instance = static_cast<VkInstance>(*instance_);
    allocator_info.vulkanApiVersion = VK_API_VERSION_1_3;
    const auto result = vmaCreateAllocator(&allocator_info, &allocator_);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("vmaCreateAllocator failed with VkResult " + std::to_string(result));
    }
}

VulkanContext::~VulkanContext()
{
    if (allocator_ != VK_NULL_HANDLE) {
        vmaDestroyAllocator(allocator_);
    }
}

AllocatedBuffer::AllocatedBuffer(
    const VmaAllocator allocator,
    const vk::DeviceSize size,
    const vk::BufferUsageFlags usage,
    const bool host_visible)
    : allocator_(allocator), size_(size)
{
    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = size;
    buffer_info.usage = static_cast<VkBufferUsageFlags>(usage);
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo allocation_info{};
    allocation_info.usage = host_visible ? VMA_MEMORY_USAGE_AUTO : VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (host_visible) {
        allocation_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }
    VmaAllocationInfo created_info{};
    const auto result = vmaCreateBuffer(allocator_, &buffer_info, &allocation_info, &buffer_, &allocation_, &created_info);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("VMA buffer allocation failed with VkResult " + std::to_string(result));
    }
    mapped_data_ = created_info.pMappedData;
}

AllocatedBuffer::~AllocatedBuffer() { reset(); }

AllocatedBuffer::AllocatedBuffer(AllocatedBuffer&& other) noexcept
    : allocator_(std::exchange(other.allocator_, {})),
      buffer_(std::exchange(other.buffer_, {})),
      allocation_(std::exchange(other.allocation_, {})),
      size_(std::exchange(other.size_, {})),
      mapped_data_(std::exchange(other.mapped_data_, {}))
{
}

AllocatedBuffer& AllocatedBuffer::operator=(AllocatedBuffer&& other) noexcept
{
    if (this != &other) {
        reset();
        allocator_ = std::exchange(other.allocator_, {});
        buffer_ = std::exchange(other.buffer_, {});
        allocation_ = std::exchange(other.allocation_, {});
        size_ = std::exchange(other.size_, {});
        mapped_data_ = std::exchange(other.mapped_data_, {});
    }
    return *this;
}

void AllocatedBuffer::reset() noexcept
{
    if (buffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, buffer_, allocation_);
    }
    allocator_ = {};
    buffer_ = {};
    allocation_ = {};
    size_ = {};
    mapped_data_ = {};
}

void AllocatedBuffer::flush(const vk::DeviceSize size)
{
    if (vmaFlushAllocation(allocator_, allocation_, 0, size) != VK_SUCCESS) {
        throw std::runtime_error("Unable to flush mapped Vulkan allocation");
    }
}

void AllocatedBuffer::invalidate(const vk::DeviceSize size)
{
    if (vmaInvalidateAllocation(allocator_, allocation_, 0, size) != VK_SUCCESS) {
        throw std::runtime_error("Unable to invalidate mapped Vulkan allocation");
    }
}

AllocatedImage::AllocatedImage(
    const VmaAllocator allocator,
    const RenderExtent extent,
    const vk::Format format,
    const vk::ImageUsageFlags usage)
    : allocator_(allocator)
{
    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = static_cast<VkFormat>(format);
    image_info.extent = {extent.width, extent.height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = static_cast<VkImageUsageFlags>(usage);
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo allocation_info{};
    allocation_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    const auto result = vmaCreateImage(allocator_, &image_info, &allocation_info, &image_, &allocation_, nullptr);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("VMA image allocation failed with VkResult " + std::to_string(result));
    }
}

AllocatedImage::~AllocatedImage() { reset(); }

AllocatedImage::AllocatedImage(AllocatedImage&& other) noexcept
    : allocator_(std::exchange(other.allocator_, {})),
      image_(std::exchange(other.image_, {})),
      allocation_(std::exchange(other.allocation_, {}))
{
}

AllocatedImage& AllocatedImage::operator=(AllocatedImage&& other) noexcept
{
    if (this != &other) {
        reset();
        allocator_ = std::exchange(other.allocator_, {});
        image_ = std::exchange(other.image_, {});
        allocation_ = std::exchange(other.allocation_, {});
    }
    return *this;
}

void AllocatedImage::reset() noexcept
{
    if (image_ != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, image_, allocation_);
    }
    allocator_ = {};
    image_ = {};
    allocation_ = {};
}

void validate_scene_frame(const SceneFrame2D& frame)
{
    if (frame.vertices.empty() || frame.indices.empty()) {
        throw std::invalid_argument("Scene frame geometry must not be empty");
    }
    if (frame.indices.size() % 3 != 0) {
        throw std::invalid_argument("Scene frame index count must describe triangles");
    }
    if (frame.indices.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::overflow_error("Scene frame index count exceeds Vulkan draw limits");
    }
    for (const auto index : frame.indices) {
        if (index >= frame.vertices.size()) {
            throw std::out_of_range("Scene frame contains an out-of-range index");
        }
    }
}

void GeometryUpload::prepare(const SceneFrame2D& frame)
{
    validate_scene_frame(frame);
    if (frame.vertices.size() > std::numeric_limits<vk::DeviceSize>::max() / sizeof(Vertex2D) ||
        frame.indices.size() > std::numeric_limits<vk::DeviceSize>::max() / sizeof(std::uint32_t)) {
        throw std::overflow_error("Scene geometry byte size overflows VkDeviceSize");
    }
    vertex_bytes_ = frame.vertices.size() * sizeof(Vertex2D);
    index_bytes_ = frame.indices.size() * sizeof(std::uint32_t);
    ensure_capacity(vertex_bytes_, index_bytes_);
    std::memcpy(vertices_staging_.mapped_data(), frame.vertices.data(), static_cast<std::size_t>(vertex_bytes_));
    std::memcpy(indices_staging_.mapped_data(), frame.indices.data(), static_cast<std::size_t>(index_bytes_));
    vertices_staging_.flush(vertex_bytes_);
    indices_staging_.flush(index_bytes_);
    index_count_ = static_cast<std::uint32_t>(frame.indices.size());
}

void GeometryUpload::ensure_capacity(const vk::DeviceSize vertex_bytes, const vk::DeviceSize index_bytes)
{
    if (vertices_staging_.size() < vertex_bytes) {
        const auto capacity = growing_capacity(vertex_bytes);
        vertices_staging_ = AllocatedBuffer{allocator_, capacity, vk::BufferUsageFlagBits::eTransferSrc, true};
        vertices_gpu_ = AllocatedBuffer{
            allocator_, capacity, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer, false};
    }
    if (indices_staging_.size() < index_bytes) {
        const auto capacity = growing_capacity(index_bytes);
        indices_staging_ = AllocatedBuffer{allocator_, capacity, vk::BufferUsageFlagBits::eTransferSrc, true};
        indices_gpu_ = AllocatedBuffer{
            allocator_, capacity, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer, false};
    }
}

void GeometryUpload::record(const vk::CommandBuffer command_buffer) const
{
    command_buffer.copyBuffer(vertices_staging_.buffer(), vertices_gpu_.buffer(), vk::BufferCopy{0, 0, vertex_bytes_});
    command_buffer.copyBuffer(indices_staging_.buffer(), indices_gpu_.buffer(), vk::BufferCopy{0, 0, index_bytes_});
    std::array<vk::BufferMemoryBarrier2, 2> barriers;
    barriers[0]
        .setSrcStageMask(vk::PipelineStageFlagBits2::eCopy)
        .setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
        .setDstStageMask(vk::PipelineStageFlagBits2::eVertexAttributeInput)
        .setDstAccessMask(vk::AccessFlagBits2::eVertexAttributeRead)
        .setBuffer(vertices_gpu_.buffer())
        .setOffset(0)
        .setSize(vertex_bytes_);
    barriers[1]
        .setSrcStageMask(vk::PipelineStageFlagBits2::eCopy)
        .setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
        .setDstStageMask(vk::PipelineStageFlagBits2::eIndexInput)
        .setDstAccessMask(vk::AccessFlagBits2::eIndexRead)
        .setBuffer(indices_gpu_.buffer())
        .setOffset(0)
        .setSize(index_bytes_);
    vk::DependencyInfo dependency;
    dependency.setBufferMemoryBarriers(barriers);
    command_buffer.pipelineBarrier2(dependency);
}

SceneDrawPass2D::SceneDrawPass2D(VulkanContext& context) : context_(context)
{
    vertex_shader_ = read_spirv(std::string{AKARI_SHADER_DIR} + "/flat_color.vert.spv");
    fragment_shader_ = read_spirv(std::string{AKARI_SHADER_DIR} + "/flat_color.frag.spv");
    vk::PushConstantRange push_constants;
    push_constants.setStageFlags(vk::ShaderStageFlagBits::eVertex).setOffset(0).setSize(sizeof(glm::mat4));
    vk::PipelineLayoutCreateInfo layout_info;
    layout_info.setPushConstantRanges(push_constants);
    pipeline_layout_ = vk::raii::PipelineLayout{context_.device(), layout_info};
}

const vk::raii::Pipeline& SceneDrawPass2D::pipeline_for(const vk::Format format)
{
    const auto found = std::ranges::find_if(pipelines_, [format](const auto& entry) { return entry.first == format; });
    if (found != pipelines_.end()) {
        return found->second;
    }

    const vk::raii::ShaderModule vertex_module{context_.device(), vk::ShaderModuleCreateInfo{{}, vertex_shader_}};
    const vk::raii::ShaderModule fragment_module{context_.device(), vk::ShaderModuleCreateInfo{{}, fragment_shader_}};
    std::array<vk::PipelineShaderStageCreateInfo, 2> stages;
    stages[0].setStage(vk::ShaderStageFlagBits::eVertex).setModule(*vertex_module).setPName("main");
    stages[1].setStage(vk::ShaderStageFlagBits::eFragment).setModule(*fragment_module).setPName("main");
    const vk::VertexInputBindingDescription binding{0, sizeof(Vertex2D), vk::VertexInputRate::eVertex};
    const std::array attributes{
        vk::VertexInputAttributeDescription{0, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex2D, position)},
        vk::VertexInputAttributeDescription{1, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex2D, color)},
    };
    vk::PipelineVertexInputStateCreateInfo vertex_input;
    vertex_input.setVertexBindingDescriptions(binding).setVertexAttributeDescriptions(attributes);
    vk::PipelineInputAssemblyStateCreateInfo input_assembly;
    input_assembly.setTopology(vk::PrimitiveTopology::eTriangleList);
    vk::PipelineViewportStateCreateInfo viewport_state;
    viewport_state.setViewportCount(1).setScissorCount(1);
    vk::PipelineRasterizationStateCreateInfo rasterization;
    rasterization.setPolygonMode(vk::PolygonMode::eFill)
        .setCullMode(vk::CullModeFlagBits::eNone)
        .setFrontFace(vk::FrontFace::eCounterClockwise)
        .setLineWidth(1.0F);
    vk::PipelineMultisampleStateCreateInfo multisampling;
    multisampling.setRasterizationSamples(vk::SampleCountFlagBits::e1);
    vk::PipelineColorBlendAttachmentState blend_attachment;
    blend_attachment.setBlendEnable(false).setColorWriteMask(
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
    vk::PipelineColorBlendStateCreateInfo color_blend;
    color_blend.setAttachments(blend_attachment);
    const std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamic_state;
    dynamic_state.setDynamicStates(dynamic_states);
    vk::PipelineRenderingCreateInfo rendering_info;
    rendering_info.setColorAttachmentFormats(format);
    vk::GraphicsPipelineCreateInfo pipeline_info;
    pipeline_info.setPNext(&rendering_info)
        .setStages(stages)
        .setPVertexInputState(&vertex_input)
        .setPInputAssemblyState(&input_assembly)
        .setPViewportState(&viewport_state)
        .setPRasterizationState(&rasterization)
        .setPMultisampleState(&multisampling)
        .setPColorBlendState(&color_blend)
        .setPDynamicState(&dynamic_state)
        .setLayout(*pipeline_layout_);
    pipelines_.emplace_back(format, vk::raii::Pipeline{context_.device(), nullptr, pipeline_info});
    return pipelines_.back().second;
}

void SceneDrawPass2D::record(
    const vk::CommandBuffer command_buffer,
    const GeometryUpload& geometry,
    const RenderTarget2D& target)
{
    geometry.record(command_buffer);
    vk::ImageMemoryBarrier2 to_attachment;
    to_attachment.setSrcStageMask(
                     target.initial_layout == vk::ImageLayout::eTransferSrcOptimal
                         ? vk::PipelineStageFlagBits2::eCopy
                         : vk::PipelineStageFlagBits2::eNone)
        .setSrcAccessMask(
            target.initial_layout == vk::ImageLayout::eTransferSrcOptimal
                ? vk::AccessFlagBits2::eTransferRead
                : vk::AccessFlagBits2::eNone)
        .setDstStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
        .setDstAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
        .setOldLayout(target.initial_layout)
        .setNewLayout(vk::ImageLayout::eColorAttachmentOptimal)
        .setImage(target.image)
        .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    vk::DependencyInfo to_attachment_dependency;
    to_attachment_dependency.setImageMemoryBarriers(to_attachment);
    command_buffer.pipelineBarrier2(to_attachment_dependency);

    vk::RenderingAttachmentInfo color_attachment;
    color_attachment.setImageView(target.view)
        .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
        .setLoadOp(vk::AttachmentLoadOp::eClear)
        .setStoreOp(vk::AttachmentStoreOp::eStore)
        .setClearValue(vk::ClearColorValue{std::array{
            target.clear_color.r, target.clear_color.g, target.clear_color.b, target.clear_color.a}});
    vk::RenderingInfo rendering_info;
    rendering_info.setRenderArea({{0, 0}, target.extent}).setLayerCount(1).setColorAttachments(color_attachment);
    command_buffer.beginRendering(rendering_info);
    command_buffer.setViewport(
        0,
        vk::Viewport{0.0F, 0.0F, static_cast<float>(target.extent.width), static_cast<float>(target.extent.height), 0.0F, 1.0F});
    command_buffer.setScissor(0, vk::Rect2D{{0, 0}, target.extent});
    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline_for(target.format));
    command_buffer.bindVertexBuffers(0, geometry.vertex_buffer(), vk::DeviceSize{0});
    command_buffer.bindIndexBuffer(geometry.index_buffer(), 0, vk::IndexType::eUint32);
    const float half_height = 1.5F;
    const float half_width = half_height * static_cast<float>(target.extent.width) / static_cast<float>(target.extent.height);
    glm::mat4 view_projection{1.0F};
    view_projection[0][0] = 1.0F / half_width;
    view_projection[1][1] = -1.0F / half_height;
    command_buffer.pushConstants<glm::mat4>(
        *pipeline_layout_, vk::ShaderStageFlagBits::eVertex, 0, view_projection);
    command_buffer.drawIndexed(geometry.index_count(), 1, 0, 0, 0);
    command_buffer.endRendering();

    vk::ImageMemoryBarrier2 to_final;
    to_final.setSrcStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
        .setSrcAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
        .setDstStageMask(
            target.final_layout == vk::ImageLayout::eTransferSrcOptimal
                ? vk::PipelineStageFlagBits2::eCopy
                : vk::PipelineStageFlagBits2::eNone)
        .setDstAccessMask(
            target.final_layout == vk::ImageLayout::eTransferSrcOptimal
                ? vk::AccessFlagBits2::eTransferRead
                : vk::AccessFlagBits2::eNone)
        .setOldLayout(vk::ImageLayout::eColorAttachmentOptimal)
        .setNewLayout(target.final_layout)
        .setImage(target.image)
        .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    vk::DependencyInfo to_final_dependency;
    to_final_dependency.setImageMemoryBarriers(to_final);
    command_buffer.pipelineBarrier2(to_final_dependency);
}

FrameScheduler::FrameScheduler(VulkanContext& context, const std::size_t frame_count) : context_(context)
{
    vk::CommandPoolCreateInfo pool_info;
    pool_info.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
        .setQueueFamilyIndex(context_.graphics_queue_family());
    command_pool_ = vk::raii::CommandPool{context_.device(), pool_info};
    vk::CommandBufferAllocateInfo allocate_info;
    allocate_info.setCommandPool(*command_pool_)
        .setLevel(vk::CommandBufferLevel::ePrimary)
        .setCommandBufferCount(static_cast<std::uint32_t>(frame_count));
    auto command_buffers = vk::raii::CommandBuffers{context_.device(), allocate_info};
    frames_.reserve(frame_count);
    for (std::size_t index = 0; index < frame_count; ++index) {
        frames_.emplace_back(context_.allocator());
        frames_.back().fence = vk::raii::Fence{
            context_.device(), vk::FenceCreateInfo{vk::FenceCreateFlagBits::eSignaled}};
        frames_.back().command_buffer = std::move(command_buffers[index]);
    }
}

FrameSlot& FrameScheduler::begin_frame()
{
    auto& slot = frames_.at(current_frame_);
    if (context_.device().waitForFences(*slot.fence, true, std::numeric_limits<std::uint64_t>::max()) != vk::Result::eSuccess) {
        throw std::runtime_error("Timed out waiting for a Vulkan frame fence");
    }
    slot.command_buffer.reset();
    return slot;
}

void FrameScheduler::submit(
    FrameSlot& slot,
    const std::span<const vk::SemaphoreSubmitInfo> waits,
    const std::span<const vk::SemaphoreSubmitInfo> signals)
{
    context_.device().resetFences(*slot.fence);
    vk::CommandBufferSubmitInfo command_info;
    command_info.setCommandBuffer(*slot.command_buffer).setDeviceMask(0);
    vk::SubmitInfo2 submit_info;
    submit_info.setWaitSemaphoreInfos(waits).setCommandBufferInfos(command_info).setSignalSemaphoreInfos(signals);
    context_.graphics_queue().submit2(submit_info, *slot.fence);
}

void FrameScheduler::wait(FrameSlot& slot) const
{
    if (context_.device().waitForFences(*slot.fence, true, std::numeric_limits<std::uint64_t>::max()) != vk::Result::eSuccess) {
        throw std::runtime_error("Timed out waiting for offscreen rendering");
    }
}

void FrameScheduler::advance() noexcept { current_frame_ = (current_frame_ + 1) % frames_.size(); }

void FrameScheduler::wait_all() const
{
    std::vector<vk::Fence> fences;
    fences.reserve(frames_.size());
    for (const auto& frame : frames_) {
        fences.push_back(*frame.fence);
    }
    if (context_.device().waitForFences(fences, true, std::numeric_limits<std::uint64_t>::max()) != vk::Result::eSuccess) {
        throw std::runtime_error("Timed out waiting for Vulkan frames");
    }
}

} // namespace akari::vulkan_detail
