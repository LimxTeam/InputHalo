#pragma once
#include "Settings/SettingsModel.h"
#include <filesystem>
#include <expected>
#include <vector>
namespace ih {
struct Profile {std::string name;std::filesystem::path path;SettingsModel settings;bool builtin{};};
std::vector<Profile> ReadProfiles(const std::filesystem::path& directory);
std::expected<SettingsModel,std::string> ImportProfile(const std::filesystem::path& path);
SettingsModel ProfileSettings(const SettingsModel& source);
void ApplyProfile(SettingsModel& target,const SettingsModel& profile);
}
