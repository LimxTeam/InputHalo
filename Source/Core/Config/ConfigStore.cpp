#include "Core/Config/ConfigStore.h"
#include <Windows.h>
#include <fstream>

namespace ih {
std::expected<ConfigLoadResult, std::string> LoadConfig(const std::filesystem::path& path) {
    try {
        if (!std::filesystem::exists(path)) return ConfigLoadResult{SettingsModel::Defaults(), {}};
        std::ifstream input(path);
        if (!input) return std::unexpected("Cannot open settings file");
        nlohmann::json source;
        try {
            input >> source;
            return ConfigLoadResult{SettingsModel::FromJson(source), {}};
        } catch (const std::exception&) {
            input.close();
            auto backup = path;
            backup += L".invalid";
            for (unsigned index = 1; std::filesystem::exists(backup); ++index) {
                backup = path;
                backup += L".invalid." + std::to_wstring(index);
            }
            std::filesystem::rename(path, backup);
            return ConfigLoadResult{SettingsModel::Defaults(), "Invalid or unsupported settings backed up; defaults restored"};
        }
    } catch (const std::exception& error) {
        return std::unexpected(error.what());
    }
}

std::expected<void, std::string> SaveConfig(const std::filesystem::path& path, const SettingsModel& settings) {
    try {
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        auto temporary = path;
        temporary += L".tmp";
        {
            std::ofstream output(temporary, std::ios::trunc);
            output.exceptions(std::ios::failbit | std::ios::badbit);
            output << settings.values.dump(2) << '\n';
            output.close();
        }
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return std::unexpected("Cannot replace settings file: " + std::to_string(GetLastError()));
        return {};
    } catch (const std::exception& error) {
        return std::unexpected(error.what());
    }
}
}
