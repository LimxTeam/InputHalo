#include "Core/Config/ConfigStore.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct TemporaryDirectory {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("InputHaloTests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryDirectory() { std::filesystem::create_directories(path); }
    ~TemporaryDirectory() { std::error_code error; std::filesystem::remove_all(path, error); }
};
}
int main() {
    try {
        const auto defaults = ih::SettingsModel::Defaults();
        Require(defaults.values["input"]["hold_ms"] == 650, "Default hold duration");
        Require(defaults.values["cursor"]["glow"] == 0.22, "Section 124 default glow");
        const auto parsed = ih::SettingsModel::FromJson(nlohmann::json::parse(R"({
            "unknown":true,"cursor":{"opacity":9,"size":-1,"style":"invalid","color":"bad"},
            "input":{"hold_ms":"wrong"},"general":{"ui_scale":0.1}})"));
        Require(parsed.values["cursor"]["opacity"] == 1, "Clamp opacity");
        Require(parsed.values["cursor"]["size"] == 4, "Clamp size");
        Require(parsed.values["cursor"]["style"] == "ring", "Invalid enum fallback");
        Require(parsed.values["cursor"]["color"] == "#75E6C8", "Invalid color fallback");
        Require(parsed.values["input"]["hold_ms"] == 650, "Wrong type fallback");
        Require(parsed.values["general"]["ui_scale"] == 0.5, "Clamp scale");
        Require(!parsed.values.contains("unknown"), "Ignore unknown fields");

        TemporaryDirectory temporary;
        const auto path = temporary.path / "settings.json";
        auto loaded = ih::LoadConfig(path);
        Require(loaded.has_value() && loaded->settings.values == defaults.values, "Missing file defaults");
        Require(ih::SaveConfig(path, defaults).has_value(), "Save defaults");
        loaded = ih::LoadConfig(path);
        Require(loaded.has_value() && loaded->settings.values == defaults.values, "Round trip");
        Require(ih::SaveConfig(path, parsed).has_value(), "Replace existing file");
        loaded = ih::LoadConfig(path);
        Require(loaded.has_value() && loaded->settings.values == parsed.values, "Replacement round trip");
        { std::ofstream output(path); output << "{broken"; }
        loaded = ih::LoadConfig(path);
        Require(loaded.has_value() && !loaded->warning.empty(), "Corrupt file recovery");
        Require(loaded->settings.values == defaults.values, "Recovered defaults");
        Require(std::filesystem::exists(temporary.path / "settings.json.invalid"), "Corrupt backup exists");
        { std::ifstream input(temporary.path / "settings.json.invalid"); std::string text; input >> text;
          Require(text == "{broken", "Backup preserves original content"); }
        { std::ofstream output(path); output << R"({"version":99})"; }
        loaded = ih::LoadConfig(path);
        Require(loaded.has_value() && !loaded->warning.empty(), "Unsupported version recovery");
        Require(std::filesystem::exists(temporary.path / "settings.json.invalid.1"), "Unique backup");
        std::cout << "Configuration tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
