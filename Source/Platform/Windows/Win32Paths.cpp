#include "Platform/Windows/Win32Paths.h"
#include <Windows.h>
#include <ShlObj.h>
#include <memory>

namespace ih::platform {
std::expected<std::filesystem::path, std::string> ExecutableDirectory() {
    wchar_t path[32768]{};
    const auto length = GetModuleFileNameW(nullptr, path, 32768);
    if (!length || length == 32768) return std::unexpected("Cannot resolve executable directory");
    return std::filesystem::path(path).parent_path();
}
std::expected<std::filesystem::path, std::string> UserDataDirectory() {
    PWSTR raw = nullptr;
    const auto status = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw);
    std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owner(raw, &CoTaskMemFree);
    if (FAILED(status)) return std::unexpected("Cannot resolve LocalAppData");
    return std::filesystem::path(raw) / L"InputHalo";
}
}
