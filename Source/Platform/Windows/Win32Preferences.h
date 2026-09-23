#pragma once
#include <Windows.h>
#include <filesystem>
#include <expected>
#include <optional>
#include "Settings/SettingsModel.h"
namespace ih::platform {
std::expected<std::optional<std::filesystem::path>,std::string> PickPreferenceFile(HWND owner,bool save,bool executable,bool chinese,const std::filesystem::path& initial={});
bool ForegroundExcluded(const SettingsModel& settings);
}
