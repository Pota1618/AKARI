#pragma once

#include <filesystem>

namespace akari::platform {

[[nodiscard]] std::filesystem::path executable_directory();

} // namespace akari::platform
