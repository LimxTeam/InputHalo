#pragma once
#include <nlohmann/json.hpp>

namespace ih {
// Version-one configuration data. Runtime feature state belongs to later milestones.
struct SettingsModel {
    nlohmann::json values;
    static SettingsModel Defaults();
    static SettingsModel FromJson(const nlohmann::json& source);
};
}
