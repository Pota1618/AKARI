#include <akari/image/png_writer.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

template <typename Exception, typename Function>
void check_throws(Function&& function, const std::string_view message)
{
    try {
        function();
        check(false, message);
    } catch (const Exception&) {
    }
}

std::uint32_t big_endian_u32(const std::array<std::uint8_t, 24>& bytes, const std::size_t offset)
{
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8U) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

} // namespace

int main()
{
    check(akari::checked_rgba_byte_size({2, 3}) == 24, "RGBA byte size");
    check_throws<std::invalid_argument>([] { (void)akari::checked_rgba_byte_size({0, 3}); }, "zero width rejected");
    check_throws<std::invalid_argument>([] { (void)akari::checked_rgba_byte_size({3, 0}); }, "zero height rejected");

    akari::ImageRgba8 invalid{{2, 2}, std::vector<std::uint8_t>(15)};
    check_throws<std::invalid_argument>([&] { invalid.validate(); }, "mismatched pixels rejected");

    const auto path = std::filesystem::path{"akari_image_io_test.png"};
    const akari::ImageRgba8 image{
        {2, 1},
        {255, 0, 0, 255, 0, 255, 0, 128},
    };
    akari::write_png(path, image);
    std::ifstream input{path, std::ios::binary};
    std::array<std::uint8_t, 24> header{};
    input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    constexpr std::array<std::uint8_t, 8> png_signature{137, 80, 78, 71, 13, 10, 26, 10};
    check(std::equal(png_signature.begin(), png_signature.end(), header.begin()), "PNG signature");
    check(big_endian_u32(header, 16) == 2 && big_endian_u32(header, 20) == 1, "PNG dimensions and row layout");
    input.close();

    if (failures != 0) {
        std::cerr << failures << " image I/O checks failed\n";
        return 1;
    }
    std::cout << "All image I/O checks passed\n";
    return 0;
}
