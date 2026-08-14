#include "executable_directory.hpp"

#include <akari/core/error.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <filesystem>
#include <string>
#include <vector>

namespace akari::platform {

std::filesystem::path executable_directory()
{
#ifdef _WIN32
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            throw AkariError{
                ErrorCategory::ShaderAsset,
                "GetModuleFileNameW failed while locating executable assets (Win32 error " +
                    std::to_string(GetLastError()) + ")"};
        }
        if (length < buffer.size() - 1) {
            return std::filesystem::path{std::wstring_view{buffer.data(), length}}.parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
#else
    throw AkariError{
        ErrorCategory::ShaderAsset,
        "Automatic executable asset discovery is not implemented on this platform; set shader_directory explicitly"};
#endif
}

} // namespace akari::platform
