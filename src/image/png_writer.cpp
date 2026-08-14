#include <akari/image/png_writer.hpp>

#include <akari/core/error.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace akari {

namespace {

void append_png_bytes(void* context, void* data, const int size)
{
    auto& bytes = *static_cast<std::vector<std::uint8_t>*>(context);
    const auto* first = static_cast<const std::uint8_t*>(data);
    bytes.insert(bytes.end(), first, first + size);
}

} // namespace

void write_png(const std::filesystem::path& path, const ImageRgba8& image)
{
    image.validate();
    if (image.extent.width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        image.extent.height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("PNG dimensions exceed stb_image_write limits");
    }

    std::vector<std::uint8_t> encoded;
    const auto stride = static_cast<int>(image.extent.width * 4U);
    if (stbi_write_png_to_func(
            append_png_bytes,
            &encoded,
            static_cast<int>(image.extent.width),
            static_cast<int>(image.extent.height),
            4,
            image.pixels.data(),
            stride) == 0) {
        throw AkariError{ErrorCategory::ImageExport, "PNG encoding failed"};
    }

    std::ofstream output{path, std::ios::binary};
    if (!output) {
        throw AkariError{ErrorCategory::ImageExport, "Unable to open PNG output: " + path.string()};
    }
    output.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    if (!output) {
        throw AkariError{ErrorCategory::ImageExport, "Unable to write PNG output: " + path.string()};
    }
}

} // namespace akari
