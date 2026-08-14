#include <akari/core/error.hpp>

#include <utility>

namespace akari {

std::string_view to_string(const ErrorCategory category) noexcept
{
    switch (category) {
    case ErrorCategory::ShaderAsset:
        return "shader_asset";
    case ErrorCategory::VulkanCapability:
        return "vulkan_capability";
    case ErrorCategory::GpuAllocation:
        return "gpu_allocation";
    case ErrorCategory::RenderSubmission:
        return "render_submission";
    case ErrorCategory::ImageExport:
        return "image_export";
    case ErrorCategory::CommandLine:
        return "command_line";
    }
    return "unknown";
}

AkariError::AkariError(const ErrorCategory category, std::string message)
    : std::runtime_error(std::move(message)), category_(category)
{
}

ErrorCategory AkariError::category() const noexcept
{
    return category_;
}

} // namespace akari
