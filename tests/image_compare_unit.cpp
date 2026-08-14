#include "support/image_compare.hpp"

#include <iostream>
#include <string_view>

namespace {

int failures{};

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

} // namespace

int main()
{
    const akari::ImageRgba8 expected{{2, 1}, {10, 20, 30, 255, 100, 110, 120, 255}};
    check(akari::test_support::compare_images(expected, expected).passes(), "identical image passes");

    auto within = expected;
    within.pixels[0] += 6;
    const auto within_result = akari::test_support::compare_images(expected, within);
    check(within_result.passes(), "threshold difference passes");
    check(within_result.maximum_rgb_difference == 6, "maximum difference reported");

    auto rgb_failure = expected;
    rgb_failure.pixels[0] = 255;
    const auto rgb_result = akari::test_support::compare_images(expected, rgb_failure);
    check(!rgb_result.passes(), "large RGB difference fails");
    check(rgb_result.differing_pixel_count == 1, "differing pixel counted");

    auto alpha_failure = expected;
    alpha_failure.pixels[3] = 0;
    check(!akari::test_support::compare_images(expected, alpha_failure).passes(), "alpha difference fails");

    const akari::ImageRgba8 different_size{{1, 1}, {0, 0, 0, 255}};
    check(
        !akari::test_support::compare_images(expected, different_size).passes(),
        "dimension difference fails");

    const auto difference = akari::test_support::difference_image(expected, rgb_failure);
    check(difference.pixels[0] == 245 && difference.pixels[3] == 255, "difference image generated");

    if (failures != 0) {
        std::cerr << failures << " image comparison checks failed\n";
        return 1;
    }
    std::cout << "All image comparison checks passed\n";
    return 0;
}
