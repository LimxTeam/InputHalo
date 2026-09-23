#include "Platform/Windows/Win32Fonts.h"
#include <Windows.h>
#include <ShlObj.h>
#include <stdexcept>
namespace ih::platform {
std::vector<std::filesystem::path> DefaultFontPaths() {
    PWSTR value{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Fonts, 0, nullptr, &value))) throw std::runtime_error("Cannot locate Windows fonts");
    const std::filesystem::path directory(value);
    CoTaskMemFree(value);
    std::vector<std::filesystem::path> result;
    for (const auto name : {L"segoeui.ttf", L"msyh.ttc", L"simsun.ttc", L"arial.ttf"}) {
        const auto path = directory / name;
        if (std::filesystem::is_regular_file(path)) result.push_back(path);
    }
    if (result.empty()) throw std::runtime_error("No supported system fonts found");
    return result;
}
}
