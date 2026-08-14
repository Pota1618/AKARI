#include "image_compare.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <vector>

namespace akari::test_support {

bool ImageComparison::passes(
    const double maximum_differing_ratio,
    const double maximum_mean_difference) const noexcept
{
    return dimensions_equal && alpha_equal && differing_pixel_ratio <= maximum_differing_ratio &&
           mean_rgb_difference <= maximum_mean_difference;
}

ImageRgba8 load_png(const std::filesystem::path& path)
{
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        throw std::runtime_error("Unable to open golden PNG: " + path.string());
    }
    const std::vector<std::uint8_t> encoded{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (encoded.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::overflow_error("PNG is too large for stb_image: " + path.string());
    }
    int width{};
    int height{};
    int channels{};
    stbi_uc* decoded = stbi_load_from_memory(
        encoded.data(), static_cast<int>(encoded.size()), &width, &height, &channels, 4);
    if (decoded == nullptr || width <= 0 || height <= 0) {
        throw std::runtime_error(
            "Unable to decode golden PNG " + path.string() + ": " + stbi_failure_reason());
    }
    const RenderExtent extent{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
    const auto byte_count = checked_rgba_byte_size(extent);
    ImageRgba8 result{extent, std::vector<std::uint8_t>(decoded, decoded + byte_count)};
    stbi_image_free(decoded);
    return result;
}

ImageComparison compare_images(
    const ImageRgba8& expected,
    const ImageRgba8& actual,
    const std::uint8_t pixel_threshold)
{
    expected.validate();
    actual.validate();
    ImageComparison result;
    result.dimensions_equal = expected.extent == actual.extent;
    if (!result.dimensions_equal) {
        result.differing_pixel_ratio = 1.0;
        return result;
    }

    result.alpha_equal = true;
    std::uint64_t total_rgb_difference{};
    const auto pixel_count = expected.pixels.size() / 4;
    for (std::size_t offset = 0; offset < expected.pixels.size(); offset += 4) {
        bool pixel_differs = false;
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const auto difference = static_cast<std::uint8_t>(std::abs(
                static_cast<int>(expected.pixels[offset + channel]) -
                static_cast<int>(actual.pixels[offset + channel])));
            result.maximum_rgb_difference = std::max(result.maximum_rgb_difference, difference);
            total_rgb_difference += difference;
            pixel_differs = pixel_differs || difference > pixel_threshold;
        }
        result.alpha_equal = result.alpha_equal && expected.pixels[offset + 3] == actual.pixels[offset + 3];
        result.differing_pixel_count += pixel_differs ? 1U : 0U;
    }
    result.mean_rgb_difference = static_cast<double>(total_rgb_difference) /
                                 static_cast<double>(pixel_count * 3);
    result.differing_pixel_ratio = static_cast<double>(result.differing_pixel_count) /
                                   static_cast<double>(pixel_count);
    return result;
}

ImageRgba8 difference_image(const ImageRgba8& expected, const ImageRgba8& actual)
{
    expected.validate();
    actual.validate();
    if (expected.extent != actual.extent) {
        throw std::invalid_argument("Cannot create a difference image for unequal dimensions");
    }
    ImageRgba8 result{expected.extent, std::vector<std::uint8_t>(expected.pixels.size())};
    for (std::size_t offset = 0; offset < expected.pixels.size(); offset += 4) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            result.pixels[offset + channel] = static_cast<std::uint8_t>(std::abs(
                static_cast<int>(expected.pixels[offset + channel]) -
                static_cast<int>(actual.pixels[offset + channel])));
        }
        result.pixels[offset + 3] = 255;
    }
    return result;
}

} // namespace akari::test_support
