#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace akari {

struct RenderExtent {
    std::uint32_t width{};
    std::uint32_t height{};

    friend bool operator==(const RenderExtent&, const RenderExtent&) = default;
};

[[nodiscard]] inline std::size_t checked_rgba_byte_size(const RenderExtent extent)
{
    if (extent.width == 0 || extent.height == 0) {
        throw std::invalid_argument("Image extent must be non-zero");
    }
    constexpr std::size_t channels = 4;
    const auto width = static_cast<std::size_t>(extent.width);
    const auto height = static_cast<std::size_t>(extent.height);
    if (height > std::numeric_limits<std::size_t>::max() / width ||
        width * height > std::numeric_limits<std::size_t>::max() / channels) {
        throw std::overflow_error("RGBA image byte size overflows size_t");
    }
    return width * height * channels;
}

struct ImageRgba8 {
    RenderExtent extent;
    std::vector<std::uint8_t> pixels;

    void validate() const
    {
        if (pixels.size() != checked_rgba_byte_size(extent)) {
            throw std::invalid_argument("RGBA image pixel count does not match its extent");
        }
    }

    friend bool operator==(const ImageRgba8&, const ImageRgba8&) = default;
};

} // namespace akari
