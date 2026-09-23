#pragma once
#include <filesystem>
#include <vector>
namespace ih::platform {
// Installed system fonts are used in place, never redistributed with the application.
std::vector<std::filesystem::path> DefaultFontPaths();
}
