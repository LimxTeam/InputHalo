#pragma once
#include "Settings/SettingsModel.h"
#include <expected>
#include <filesystem>
#include <string>

namespace ih {
struct ConfigLoadResult {
    SettingsModel settings;
    std::string warning;
};
std::expected<ConfigLoadResult, std::string> LoadConfig(const std::filesystem::path& path);
std::expected<void, std::string> SaveConfig(const std::filesystem::path& path, const SettingsModel& settings);
}
