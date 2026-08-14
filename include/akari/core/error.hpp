#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace akari {

enum class ErrorCategory {
    ShaderAsset,
    VulkanCapability,
    GpuAllocation,
    RenderSubmission,
    ImageExport,
    CommandLine,
};

[[nodiscard]] std::string_view to_string(ErrorCategory category) noexcept;

class AkariError final : public std::runtime_error {
public:
    AkariError(ErrorCategory category, std::string message);

    [[nodiscard]] ErrorCategory category() const noexcept;

private:
    ErrorCategory category_;
};

} // namespace akari
