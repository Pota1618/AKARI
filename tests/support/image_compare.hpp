#pragma once

#include <akari/image/image_rgba8.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace akari::test_support {

struct ImageComparison {
    bool dimensions_equal{};
    bool alpha_equal{};
    std::uint8_t maximum_rgb_difference{};
    double mean_rgb_difference{};
    std::size_t differing_pixel_count{};
    double differing_pixel_ratio{};

    [[nodiscard]] bool passes(
        double maximum_differing_ratio = 0.01,
        double maximum_mean_difference = 1.0) const noexcept;
};

[[nodiscard]] ImageRgba8 load_png(const std::filesystem::path& path);
[[nodiscard]] ImageComparison compare_images(
    const ImageRgba8& expected,
    const ImageRgba8& actual,
    std::uint8_t pixel_threshold = 8);
[[nodiscard]] ImageRgba8 difference_image(const ImageRgba8& expected, const ImageRgba8& actual);

} // namespace akari::test_support
