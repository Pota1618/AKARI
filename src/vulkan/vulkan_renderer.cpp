#include <akari/vulkan/vulkan_renderer.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <glm/mat4x4.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace akari {
namespace {

constexpr std::size_t frames_in_flight = 2;
constexpr vk::DeviceSize vertex_buffer_size = 1024 * 1024;
constexpr vk::DeviceSize index_buffer_size = 1024 * 1024;
constexpr std::array required_device_extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

struct QueueFamilies {
    std::optional<std::uint32_t> graphics;
    std::optional<std::uint32_t> present;

    [[nodiscard]] bool complete() const noexcept
    {
        return graphics.has_value() && present.has_value();
    }
};

struct MappedBuffer {
    vk::raii::Buffer buffer{nullptr};
    vk::raii::DeviceMemory memory{nullptr};
    void* mapped{};

    MappedBuffer() = default;
    MappedBuffer(const MappedBuffer&) = delete;
    MappedBuffer& operator=(const MappedBuffer&) = delete;

    MappedBuffer(MappedBuffer&& other) noexcept
        : buffer(std::move(other.buffer)), memory(std::move(other.memory)), mapped(std::exchange(other.mapped, nullptr))
    {
    }

    MappedBuffer& operator=(MappedBuffer&& other) noexcept
    {
        if (this != &other) {
            unmap();
            buffer = std::move(other.buffer);
            memory = std::move(other.memory);
            mapped = std::exchange(other.mapped, nullptr);
        }
        return *this;
    }

    ~MappedBuffer()
    {
        unmap();
    }

    void unmap() noexcept
    {
        if (mapped != nullptr && *memory) {
            memory.unmapMemory();
            mapped = nullptr;
        }
    }
};

struct FrameResources {
    MappedBuffer vertices;
    MappedBuffer indices;
    vk::raii::Semaphore image_available{nullptr};
    vk::raii::Fence in_flight{nullptr};
    vk::raii::CommandBuffer command_buffer{nullptr};
};

std::vector<std::uint32_t> read_spirv(const std::string& path)
{
    std::ifstream input{path, std::ios::ate | std::ios::binary};
    if (!input) {
        throw std::runtime_error("Unable to open SPIR-V shader: " + path);
    }
    const auto byte_count = input.tellg();
    if (byte_count <= 0 || byte_count % 4 != 0) {
        throw std::runtime_error("Invalid SPIR-V shader size: " + path);
    }
    std::vector<std::uint32_t> code(static_cast<std::size_t>(byte_count) / sizeof(std::uint32_t));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(code.data()), byte_count);
    if (!input) {
        throw std::runtime_error("Unable to read SPIR-V shader: " + path);
    }
    return code;
}

bool contains_layer(const std::vector<vk::LayerProperties>& layers, const char* name)
{
    return std::ranges::any_of(layers, [name](const auto& layer) {
        return std::strcmp(layer.layerName, name) == 0;
    });
}

bool contains_extension(const std::vector<vk::ExtensionProperties>& extensions, const char* name)
{
    return std::ranges::any_of(extensions, [name](const auto& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

} // namespace

class VulkanRenderer::Impl {
public:
    Impl(GLFWwindow* window, const VulkanRendererOptions options)
        : window_(window), context_()
    {
        if (window_ == nullptr) {
            throw std::invalid_argument("VulkanRenderer requires a GLFW window");
        }
        create_instance(options.enable_validation);
        create_surface();
        select_physical_device();
        create_device();
        create_command_pool_and_frames();
        recreate_swapchain();
    }

    ~Impl()
    {
        if (*device_) {
            try {
                present_queue_.waitIdle();
                device_.waitIdle();
            } catch (...) {
            }
        }
    }

    void draw(const SceneFrame2D& frame)
    {
        if (frame.vertices.empty() || frame.indices.empty()) {
            throw std::invalid_argument("Scene frame must contain indexed triangles");
        }
        const auto vertex_bytes = frame.vertices.size() * sizeof(Vertex2D);
        const auto index_bytes = frame.indices.size() * sizeof(std::uint32_t);
        if (vertex_bytes > vertex_buffer_size || index_bytes > index_buffer_size) {
            throw std::runtime_error("Scene frame exceeds the M1 upload buffer capacity");
        }

        auto& resources = frames_.at(current_frame_);
        const auto frame_wait =
            device_.waitForFences({*resources.in_flight}, true, std::numeric_limits<std::uint64_t>::max());
        if (frame_wait != vk::Result::eSuccess) {
            throw std::runtime_error("Timed out waiting for the current Vulkan frame");
        }

        vk::ResultValue<std::uint32_t> acquisition{vk::Result::eSuccess, 0};
        try {
            acquisition = swapchain_.acquireNextImage(
                std::numeric_limits<std::uint64_t>::max(), *resources.image_available, nullptr);
        } catch (const vk::OutOfDateKHRError&) {
            recreate_swapchain();
            return;
        }

        const auto image_index = acquisition.value;
        if (image_fences_.at(image_index)) {
            const auto image_wait = device_.waitForFences(
                {image_fences_.at(image_index)}, true, std::numeric_limits<std::uint64_t>::max());
            if (image_wait != vk::Result::eSuccess) {
                throw std::runtime_error("Timed out waiting for a Vulkan swapchain image");
            }
        }
        image_fences_.at(image_index) = *resources.in_flight;

        std::memcpy(resources.vertices.mapped, frame.vertices.data(), vertex_bytes);
        std::memcpy(resources.indices.mapped, frame.indices.data(), index_bytes);

        device_.resetFences({*resources.in_flight});
        resources.command_buffer.reset();
        record_commands(resources, image_index, static_cast<std::uint32_t>(frame.indices.size()));
        const vk::Semaphore render_finished = *render_finished_semaphores_.at(image_index);
        submit(resources, render_finished);

        const vk::SwapchainKHR raw_swapchain = *swapchain_;
        vk::PresentInfoKHR present_info;
        present_info.setWaitSemaphores(render_finished)
            .setSwapchains(raw_swapchain)
            .setImageIndices(image_index);

        bool should_recreate = acquisition.result == vk::Result::eSuboptimalKHR;
        try {
            should_recreate = should_recreate || present_queue_.presentKHR(present_info) == vk::Result::eSuboptimalKHR;
        } catch (const vk::OutOfDateKHRError&) {
            should_recreate = true;
        }

        current_frame_ = (current_frame_ + 1) % frames_in_flight;
        if (should_recreate) {
            recreate_swapchain();
        }
    }

    void wait_idle()
    {
        present_queue_.waitIdle();
        device_.waitIdle();
    }

    [[nodiscard]] std::size_t validation_error_count() const noexcept
    {
        return validation_errors_.load();
    }

    [[nodiscard]] const char* device_name() const noexcept
    {
        return device_name_.c_str();
    }

private:
    static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
        const VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        const VkDebugUtilsMessageTypeFlagsEXT,
        const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
        void* user_data)
    {
        auto* self = static_cast<Impl*>(user_data);
        if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
            self->validation_errors_.fetch_add(1);
        }
        std::cerr << "[Vulkan "
                  << (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? "error" : "validation")
                  << "] " << callback_data->pMessage << '\n';
        return VK_FALSE;
    }

    void create_instance(const bool request_validation)
    {
        if (glfwVulkanSupported() != GLFW_TRUE) {
            throw std::runtime_error("GLFW reports that Vulkan is unavailable");
        }

        std::uint32_t glfw_extension_count = 0;
        const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
        if (glfw_extensions == nullptr || glfw_extension_count == 0) {
            throw std::runtime_error("GLFW did not provide Vulkan surface extensions");
        }
        std::vector<const char*> extensions(glfw_extensions, glfw_extensions + glfw_extension_count);

        const auto available_extensions = context_.enumerateInstanceExtensionProperties();
        const auto available_layers = context_.enumerateInstanceLayerProperties();
        validation_enabled_ = request_validation && contains_layer(available_layers, "VK_LAYER_KHRONOS_validation") &&
                              contains_extension(available_extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        if (request_validation && !validation_enabled_) {
            std::cerr << "Vulkan validation requested but VK_LAYER_KHRONOS_validation or VK_EXT_debug_utils is unavailable\n";
        }
        if (validation_enabled_) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        vk::ApplicationInfo application_info;
        application_info.setPApplicationName("AKARI Preview")
            .setApplicationVersion(VK_MAKE_API_VERSION(0, 0, 1, 0))
            .setPEngineName("AKARI")
            .setEngineVersion(VK_MAKE_API_VERSION(0, 0, 1, 0))
            .setApiVersion(VK_API_VERSION_1_3);

        const std::array validation_layers{"VK_LAYER_KHRONOS_validation"};
        vk::InstanceCreateInfo create_info;
        create_info.setPApplicationInfo(&application_info).setPEnabledExtensionNames(extensions);
        if (validation_enabled_) {
            create_info.setPEnabledLayerNames(validation_layers);
        }
        instance_ = vk::raii::Instance{context_, create_info};

        if (validation_enabled_) {
            vk::DebugUtilsMessengerCreateInfoEXT messenger_info;
            messenger_info
                .setMessageSeverity(
                    vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                    vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)
                .setMessageType(
                    vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                    vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                    vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance)
                .setPfnUserCallback(debug_callback)
                .setPUserData(this);
            debug_messenger_ = vk::raii::DebugUtilsMessengerEXT{instance_, messenger_info};
        }
    }

    void create_surface()
    {
        VkSurfaceKHR raw_surface = VK_NULL_HANDLE;
        const VkResult result = glfwCreateWindowSurface(
            static_cast<VkInstance>(*instance_), window_, nullptr, &raw_surface);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("glfwCreateWindowSurface failed with VkResult " + std::to_string(result));
        }
        surface_ = vk::raii::SurfaceKHR{instance_, raw_surface};
    }

    [[nodiscard]] QueueFamilies find_queue_families(const vk::raii::PhysicalDevice& candidate) const
    {
        QueueFamilies result;
        const auto families = candidate.getQueueFamilyProperties();
        for (std::uint32_t index = 0; index < families.size(); ++index) {
            if (families[index].queueCount > 0 &&
                (families[index].queueFlags & vk::QueueFlagBits::eGraphics) != vk::QueueFlags{}) {
                result.graphics = index;
            }
            if (families[index].queueCount > 0 && candidate.getSurfaceSupportKHR(index, *surface_)) {
                result.present = index;
            }
            if (result.complete()) {
                break;
            }
        }
        return result;
    }

    [[nodiscard]] bool device_extensions_supported(const vk::raii::PhysicalDevice& candidate) const
    {
        const auto extensions = candidate.enumerateDeviceExtensionProperties();
        return std::ranges::all_of(required_device_extensions, [&extensions](const char* required) {
            return contains_extension(extensions, required);
        });
    }

    void select_physical_device()
    {
        for (const auto& candidate : instance_.enumeratePhysicalDevices()) {
            const auto properties = candidate.getProperties();
            if (VK_API_VERSION_MAJOR(properties.apiVersion) < 1 ||
                (VK_API_VERSION_MAJOR(properties.apiVersion) == 1 && VK_API_VERSION_MINOR(properties.apiVersion) < 3)) {
                continue;
            }
            const auto queues = find_queue_families(candidate);
            if (!queues.complete() || !device_extensions_supported(candidate)) {
                continue;
            }
            const auto formats = candidate.getSurfaceFormatsKHR(*surface_);
            const auto present_modes = candidate.getSurfacePresentModesKHR(*surface_);
            if (formats.empty() || present_modes.empty()) {
                continue;
            }
            const auto feature_chain =
                candidate.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features>();
            const auto& features13 = feature_chain.get<vk::PhysicalDeviceVulkan13Features>();
            if (!features13.dynamicRendering || !features13.synchronization2) {
                continue;
            }
            physical_device_ = candidate;
            queue_families_ = queues;
            device_name_ = properties.deviceName.data();
            return;
        }
        throw std::runtime_error("No Vulkan 1.3 device supports graphics, presentation, dynamic rendering, and synchronization2");
    }

    void create_device()
    {
        const std::set unique_families{*queue_families_.graphics, *queue_families_.present};
        constexpr float priority = 1.0F;
        std::vector<vk::DeviceQueueCreateInfo> queue_infos;
        queue_infos.reserve(unique_families.size());
        for (const auto family : unique_families) {
            vk::DeviceQueueCreateInfo queue_info;
            queue_info.setQueueFamilyIndex(family).setQueuePriorities(priority);
            queue_infos.push_back(queue_info);
        }

        vk::PhysicalDeviceVulkan13Features features13;
        features13.setDynamicRendering(true).setSynchronization2(true);
        vk::DeviceCreateInfo create_info;
        create_info.setPNext(&features13)
            .setQueueCreateInfos(queue_infos)
            .setPEnabledExtensionNames(required_device_extensions);
        device_ = vk::raii::Device{physical_device_, create_info};
        graphics_queue_ = vk::raii::Queue{device_, *queue_families_.graphics, 0};
        present_queue_ = vk::raii::Queue{device_, *queue_families_.present, 0};
    }

    [[nodiscard]] std::uint32_t find_memory_type(const std::uint32_t bits, const vk::MemoryPropertyFlags properties) const
    {
        const auto memory_properties = physical_device_.getMemoryProperties();
        for (std::uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
            if ((bits & (1U << index)) != 0 &&
                (memory_properties.memoryTypes[index].propertyFlags & properties) == properties) {
                return index;
            }
        }
        throw std::runtime_error("No suitable Vulkan memory type found");
    }

    [[nodiscard]] MappedBuffer create_mapped_buffer(const vk::DeviceSize size, const vk::BufferUsageFlags usage)
    {
        MappedBuffer result;
        vk::BufferCreateInfo buffer_info;
        buffer_info.setSize(size).setUsage(usage).setSharingMode(vk::SharingMode::eExclusive);
        result.buffer = vk::raii::Buffer{device_, buffer_info};
        const auto requirements = result.buffer.getMemoryRequirements();

        vk::MemoryAllocateInfo allocation_info;
        allocation_info.setAllocationSize(requirements.size)
            .setMemoryTypeIndex(find_memory_type(
                requirements.memoryTypeBits,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));
        result.memory = vk::raii::DeviceMemory{device_, allocation_info};
        result.buffer.bindMemory(*result.memory, 0);
        result.mapped = result.memory.mapMemory(0, size);
        return result;
    }

    void create_command_pool_and_frames()
    {
        vk::CommandPoolCreateInfo pool_info;
        pool_info.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
            .setQueueFamilyIndex(*queue_families_.graphics);
        command_pool_ = vk::raii::CommandPool{device_, pool_info};

        vk::CommandBufferAllocateInfo allocation_info;
        allocation_info.setCommandPool(*command_pool_)
            .setLevel(vk::CommandBufferLevel::ePrimary)
            .setCommandBufferCount(static_cast<std::uint32_t>(frames_in_flight));
        auto command_buffers = device_.allocateCommandBuffers(allocation_info);

        frames_.reserve(frames_in_flight);
        for (std::size_t index = 0; index < frames_in_flight; ++index) {
            FrameResources resources;
            resources.vertices = create_mapped_buffer(vertex_buffer_size, vk::BufferUsageFlagBits::eVertexBuffer);
            resources.indices = create_mapped_buffer(index_buffer_size, vk::BufferUsageFlagBits::eIndexBuffer);
            resources.image_available = vk::raii::Semaphore{device_, vk::SemaphoreCreateInfo{}};
            vk::FenceCreateInfo fence_info;
            fence_info.setFlags(vk::FenceCreateFlagBits::eSignaled);
            resources.in_flight = vk::raii::Fence{device_, fence_info};
            resources.command_buffer = std::move(command_buffers.at(index));
            frames_.push_back(std::move(resources));
        }
    }

    [[nodiscard]] vk::SurfaceFormatKHR choose_surface_format(const std::vector<vk::SurfaceFormatKHR>& formats) const
    {
        const auto preferred = std::ranges::find_if(formats, [](const auto& format) {
            return format.format == vk::Format::eB8G8R8A8Srgb &&
                   format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
        });
        return preferred != formats.end() ? *preferred : formats.front();
    }

    [[nodiscard]] vk::PresentModeKHR choose_present_mode(const std::vector<vk::PresentModeKHR>& modes) const
    {
        return std::ranges::find(modes, vk::PresentModeKHR::eMailbox) != modes.end()
                   ? vk::PresentModeKHR::eMailbox
                   : vk::PresentModeKHR::eFifo;
    }

    [[nodiscard]] vk::Extent2D choose_extent(const vk::SurfaceCapabilitiesKHR& capabilities) const
    {
        if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
            return capabilities.currentExtent;
        }
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window_, &width, &height);
        return {
            std::clamp(static_cast<std::uint32_t>(width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
            std::clamp(static_cast<std::uint32_t>(height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
        };
    }

    void wait_for_nonzero_framebuffer() const
    {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window_, &width, &height);
        while ((width == 0 || height == 0) && glfwWindowShouldClose(window_) == GLFW_FALSE) {
            glfwWaitEvents();
            glfwGetFramebufferSize(window_, &width, &height);
        }
    }

    void recreate_swapchain()
    {
        wait_for_nonzero_framebuffer();
        if (glfwWindowShouldClose(window_) == GLFW_TRUE) {
            return;
        }
        present_queue_.waitIdle();
        device_.waitIdle();

        const auto capabilities = physical_device_.getSurfaceCapabilitiesKHR(*surface_);
        const auto format = choose_surface_format(physical_device_.getSurfaceFormatsKHR(*surface_));
        const auto present_mode = choose_present_mode(physical_device_.getSurfacePresentModesKHR(*surface_));
        const auto extent = choose_extent(capabilities);
        std::uint32_t image_count = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0) {
            image_count = std::min(image_count, capabilities.maxImageCount);
        }

        const std::array queue_indices{*queue_families_.graphics, *queue_families_.present};
        vk::SwapchainCreateInfoKHR create_info;
        create_info.setSurface(*surface_)
            .setMinImageCount(image_count)
            .setImageFormat(format.format)
            .setImageColorSpace(format.colorSpace)
            .setImageExtent(extent)
            .setImageArrayLayers(1)
            .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
            .setPreTransform(capabilities.currentTransform)
            .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
            .setPresentMode(present_mode)
            .setClipped(true)
            .setOldSwapchain(*swapchain_);
        if (queue_families_.graphics != queue_families_.present) {
            create_info.setImageSharingMode(vk::SharingMode::eConcurrent).setQueueFamilyIndices(queue_indices);
        } else {
            create_info.setImageSharingMode(vk::SharingMode::eExclusive);
        }

        vk::raii::SwapchainKHR new_swapchain{device_, create_info};
        auto new_images = new_swapchain.getImages();
        std::vector<vk::raii::ImageView> new_views;
        new_views.reserve(new_images.size());
        for (const auto image : new_images) {
            vk::ImageViewCreateInfo view_info;
            view_info.setImage(image)
                .setViewType(vk::ImageViewType::e2D)
                .setFormat(format.format)
                .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
            new_views.emplace_back(device_, view_info);
        }

        swapchain_ = std::move(new_swapchain);
        swapchain_images_ = std::move(new_images);
        swapchain_views_ = std::move(new_views);
        render_finished_semaphores_.clear();
        render_finished_semaphores_.reserve(swapchain_images_.size());
        for (std::size_t index = 0; index < swapchain_images_.size(); ++index) {
            render_finished_semaphores_.emplace_back(device_, vk::SemaphoreCreateInfo{});
        }
        swapchain_extent_ = extent;
        const bool format_changed = swapchain_format_ != format.format;
        swapchain_format_ = format.format;
        image_fences_.assign(swapchain_images_.size(), nullptr);
        image_initialized_.assign(swapchain_images_.size(), false);
        if (!*pipeline_ || format_changed) {
            create_pipeline();
        }
    }

    void create_pipeline()
    {
        const auto vertex_code = read_spirv(std::string{AKARI_SHADER_DIR} + "/flat_color.vert.spv");
        const auto fragment_code = read_spirv(std::string{AKARI_SHADER_DIR} + "/flat_color.frag.spv");

        vk::ShaderModuleCreateInfo vertex_module_info;
        vertex_module_info.setCode(vertex_code);
        vk::ShaderModuleCreateInfo fragment_module_info;
        fragment_module_info.setCode(fragment_code);
        const vk::raii::ShaderModule vertex_module{device_, vertex_module_info};
        const vk::raii::ShaderModule fragment_module{device_, fragment_module_info};

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

        vk::PushConstantRange push_constants;
        push_constants.setStageFlags(vk::ShaderStageFlagBits::eVertex).setOffset(0).setSize(sizeof(glm::mat4));
        vk::PipelineLayoutCreateInfo layout_info;
        layout_info.setPushConstantRanges(push_constants);
        pipeline_layout_ = vk::raii::PipelineLayout{device_, layout_info};

        vk::PipelineRenderingCreateInfo rendering_info;
        rendering_info.setColorAttachmentFormats(swapchain_format_);
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
        pipeline_ = vk::raii::Pipeline{device_, nullptr, pipeline_info};
    }

    void record_commands(FrameResources& resources, const std::uint32_t image_index, const std::uint32_t index_count)
    {
        vk::CommandBufferBeginInfo begin_info;
        begin_info.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        resources.command_buffer.begin(begin_info);

        vk::ImageMemoryBarrier2 to_attachment;
        to_attachment.setSrcStageMask(vk::PipelineStageFlagBits2::eNone)
            .setSrcAccessMask(vk::AccessFlagBits2::eNone)
            .setDstStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
            .setDstAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
            .setOldLayout(image_initialized_.at(image_index) ? vk::ImageLayout::ePresentSrcKHR : vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eColorAttachmentOptimal)
            .setImage(swapchain_images_.at(image_index))
            .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
        vk::DependencyInfo to_attachment_dependency;
        to_attachment_dependency.setImageMemoryBarriers(to_attachment);
        resources.command_buffer.pipelineBarrier2(to_attachment_dependency);

        vk::RenderingAttachmentInfo color_attachment;
        color_attachment.setImageView(*swapchain_views_.at(image_index))
            .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
            .setLoadOp(vk::AttachmentLoadOp::eClear)
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setClearValue(vk::ClearColorValue{std::array{0.035F, 0.047F, 0.075F, 1.0F}});
        vk::RenderingInfo rendering_info;
        rendering_info.setRenderArea({{0, 0}, swapchain_extent_})
            .setLayerCount(1)
            .setColorAttachments(color_attachment);
        resources.command_buffer.beginRendering(rendering_info);

        const vk::Viewport viewport{
            0.0F,
            0.0F,
            static_cast<float>(swapchain_extent_.width),
            static_cast<float>(swapchain_extent_.height),
            0.0F,
            1.0F,
        };
        const vk::Rect2D scissor{{0, 0}, swapchain_extent_};
        resources.command_buffer.setViewport(0, viewport);
        resources.command_buffer.setScissor(0, scissor);
        resources.command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline_);
        resources.command_buffer.bindVertexBuffers(0, {*resources.vertices.buffer}, {0});
        resources.command_buffer.bindIndexBuffer(*resources.indices.buffer, 0, vk::IndexType::eUint32);

        const float half_height = 1.5F;
        const float half_width = half_height * static_cast<float>(swapchain_extent_.width) /
                                 static_cast<float>(swapchain_extent_.height);
        glm::mat4 view_projection{1.0F};
        view_projection[0][0] = 1.0F / half_width;
        view_projection[1][1] = -1.0F / half_height;
        resources.command_buffer.pushConstants<glm::mat4>(
            *pipeline_layout_, vk::ShaderStageFlagBits::eVertex, 0, view_projection);
        resources.command_buffer.drawIndexed(index_count, 1, 0, 0, 0);
        resources.command_buffer.endRendering();

        vk::ImageMemoryBarrier2 to_present;
        to_present.setSrcStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
            .setSrcAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
            .setDstStageMask(vk::PipelineStageFlagBits2::eNone)
            .setDstAccessMask(vk::AccessFlagBits2::eNone)
            .setOldLayout(vk::ImageLayout::eColorAttachmentOptimal)
            .setNewLayout(vk::ImageLayout::ePresentSrcKHR)
            .setImage(swapchain_images_.at(image_index))
            .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
        vk::DependencyInfo to_present_dependency;
        to_present_dependency.setImageMemoryBarriers(to_present);
        resources.command_buffer.pipelineBarrier2(to_present_dependency);
        resources.command_buffer.end();
        image_initialized_.at(image_index) = true;
    }

    void submit(FrameResources& resources, const vk::Semaphore render_finished)
    {
        vk::SemaphoreSubmitInfo wait_info;
        wait_info.setSemaphore(*resources.image_available)
            .setStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
            .setDeviceIndex(0);
        vk::CommandBufferSubmitInfo command_info;
        command_info.setCommandBuffer(*resources.command_buffer).setDeviceMask(0);
        vk::SemaphoreSubmitInfo signal_info;
        signal_info.setSemaphore(render_finished)
            .setStageMask(vk::PipelineStageFlagBits2::eAllGraphics)
            .setDeviceIndex(0);
        vk::SubmitInfo2 submit_info;
        submit_info.setWaitSemaphoreInfos(wait_info)
            .setCommandBufferInfos(command_info)
            .setSignalSemaphoreInfos(signal_info);
        graphics_queue_.submit2(submit_info, *resources.in_flight);
    }

    GLFWwindow* window_{};
    std::atomic_size_t validation_errors_{};
    std::string device_name_;
    bool validation_enabled_{};
    vk::raii::Context context_;
    vk::raii::Instance instance_{nullptr};
    vk::raii::DebugUtilsMessengerEXT debug_messenger_{nullptr};
    vk::raii::SurfaceKHR surface_{nullptr};
    vk::raii::PhysicalDevice physical_device_{nullptr};
    vk::raii::Device device_{nullptr};
    vk::raii::Queue graphics_queue_{nullptr};
    vk::raii::Queue present_queue_{nullptr};
    vk::raii::CommandPool command_pool_{nullptr};
    std::vector<FrameResources> frames_;
    vk::raii::SwapchainKHR swapchain_{nullptr};
    std::vector<vk::Image> swapchain_images_;
    std::vector<vk::raii::ImageView> swapchain_views_;
    std::vector<vk::raii::Semaphore> render_finished_semaphores_;
    vk::raii::PipelineLayout pipeline_layout_{nullptr};
    vk::raii::Pipeline pipeline_{nullptr};
    std::vector<vk::Fence> image_fences_;
    std::vector<bool> image_initialized_;
    QueueFamilies queue_families_;
    vk::Extent2D swapchain_extent_{};
    vk::Format swapchain_format_{vk::Format::eUndefined};
    std::size_t current_frame_{};
};

VulkanRenderer::VulkanRenderer(GLFWwindow* window, const VulkanRendererOptions options)
    : impl_(std::make_unique<Impl>(window, options))
{
}

VulkanRenderer::~VulkanRenderer() = default;
VulkanRenderer::VulkanRenderer(VulkanRenderer&&) noexcept = default;
VulkanRenderer& VulkanRenderer::operator=(VulkanRenderer&&) noexcept = default;

void VulkanRenderer::draw(const SceneFrame2D& frame)
{
    impl_->draw(frame);
}

void VulkanRenderer::wait_idle()
{
    impl_->wait_idle();
}

std::size_t VulkanRenderer::validation_error_count() const noexcept
{
    return impl_->validation_error_count();
}

const char* VulkanRenderer::device_name() const noexcept
{
    return impl_->device_name();
}

} // namespace akari
