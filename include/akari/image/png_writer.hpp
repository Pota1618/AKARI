#pragma once

#include <akari/image/image_rgba8.hpp>

#include <filesystem>

namespace akari {

void write_png(const std::filesystem::path& path, const ImageRgba8& image);

} // namespace akari
