#include "Settings/SettingsModel.h"
#include "Input/Keys.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ih {
SettingsModel SettingsModel::Defaults() {
    return {nlohmann::json::parse(R"({
      "version": 1,
      "general": {"run_at_startup":false,"start_minimized":false,"language":"zh-CN","overlay_monitor":"follow_cursor","accent":"#75E6C8"},
      "input": {"keyboard_enabled":true,"shortcut_only":true,"mouse_enabled":true,"anchor":"bottom_center","offset_x":0,"offset_y":48,"scale":1.0,"hold_ms":650,"fade_ms":180,"opacity":0.92,"show_wheel":true,"show_side_buttons":true},
      "cursor": {"enabled":true,"style":"ring","size":42,"color":"#75E6C8","opacity":0.82,"border_width":2.0,"glow":0.22,"click_ripple":true,"left_color":"#75E6C8","right_color":"#FF8FA3"},
      "drawing": {"enabled":true,"hotkey":"Ctrl+Shift+Space","color":"#AA9AFF","width":2.0,"shape":"rectangle"},
      "exclusions": {"apps":[],"hotkeys":[]},
      "laser": {"enabled":true,"mode":"trail","hotkey":"Ctrl+Alt+Space","color":"#FF5A5F","width":5.0,"glow":0.35,"fade_ms":1200}
    })")};
}

SettingsModel SettingsModel::FromJson(const nlohmann::json& source) {
    if (!source.is_object()) throw std::runtime_error("Settings root must be an object");
    if (source.contains("version") && source["version"] != 1)
        throw std::runtime_error("Unsupported settings version");
    auto result = Defaults();
    for (auto& [section, fields] : result.values.items()) {
        if (!fields.is_object() || !source.contains(section) || !source[section].is_object()) continue;
        for (auto& [key, value] : fields.items()) {
            const auto& object = source[section];
            if (!object.contains(key)) continue;
            const auto& candidate = object[key];
            if ((value.is_number() && candidate.is_number()) || value.type() == candidate.type())
                value = candidate;
        }
    }
    auto clamp = [&](const char* section, const char* key, double low, double high) {
        auto& value = result.values[section][key];
        const auto number = value.get<double>();
        value = std::isfinite(number) ? std::clamp(number, low, high) : low;
    };

    clamp("input", "scale", 0.5, 3.0);
    clamp("input", "opacity", 0, 1);
    clamp("input", "offset_x", -10000, 10000);
    clamp("input", "offset_y", -10000, 10000);
    clamp("input", "hold_ms", 0, 10000);
    clamp("input", "fade_ms", 0, 5000);
    clamp("cursor", "size", 4, 256);
    clamp("cursor", "opacity", 0, 1);
    clamp("cursor", "border_width", 0, 32);
    clamp("cursor", "glow", 0, 1);
    clamp("drawing", "width", 1, 12);
    clamp("laser", "width", 1, 64);
    clamp("laser", "glow", 0, 1);
    clamp("laser", "fade_ms", 100, 10000);
    const auto defaults = Defaults();
    auto choice = [&](const char* section, const char* key, std::initializer_list<std::string_view> allowed) {
        const auto value = result.values[section][key].get<std::string>();
        if (std::find(allowed.begin(), allowed.end(), value) == allowed.end())
            result.values[section][key] = defaults.values[section][key];
    };
    choice("cursor", "style", {"ring", "halo", "spot"});
    choice("general", "language", {"zh-CN", "en"});
    choice("laser", "mode", {"trail", "pointer"});
    choice("drawing", "shape", {"rectangle", "arrow", "ellipse"});
    for(const char* field:{"apps","hotkeys"}){
        auto clean=nlohmann::json::array();
        for(const auto& item:result.values["exclusions"][field]){
            if(!item.is_string()||clean.size()>=128)continue;
            auto value=item.get<std::string>();
            if(value.empty()||value.size()>260||value.find_first_of("\r\n\t")!=std::string::npos)continue;
            if(std::string_view(field)=="hotkeys"){
                const auto key=input::ParseHotkey(value);
                if(!key.Valid()||key.primary>=input::Key::MouseLeft)continue;
                value=input::FormatHotkey(key);
            }else{
                if(value.find_first_of("/\\:*?\"<>|")!=std::string::npos)continue;
                std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return c>='A'&&c<='Z'?static_cast<char>(c+32):static_cast<char>(c);});
                if(!value.ends_with(".exe"))continue;
            }
            if(std::find(clean.begin(),clean.end(),value)==clean.end())clean.push_back(value);
        }
        result.values["exclusions"][field]=std::move(clean);
    }
    choice("input", "anchor", {"bottom_left", "bottom_center", "bottom_right", "top_left", "top_center", "top_right"});
    choice("general", "overlay_monitor", {"follow_cursor", "primary", "monitor_1", "monitor_2", "monitor_3", "monitor_4", "monitor_5", "monitor_6", "monitor_7", "monitor_8"});
    for (const auto section : {"general", "cursor", "laser", "drawing"}) {
        const auto key = std::string_view(section) == "general" ? "accent" : "color";
        const auto color = result.values[section][key].get<std::string>();
        if (color.size() != 7 || color.front() != '#' ||
            color.find_first_not_of("0123456789abcdefABCDEF", 1) != std::string::npos)
            result.values[section][key] = defaults.values[section][key];
    }
    for (const auto key : {"left_color", "right_color"}) {
        const auto color = result.values["cursor"][key].get<std::string>();
        if (color.size() != 7 || color.front() != '#' || color.find_first_not_of("0123456789abcdefABCDEF", 1) != std::string::npos)
            result.values["cursor"][key] = defaults.values["cursor"][key];
    }
    const auto hotkey=input::ParseHotkey(result.values["laser"]["hotkey"].get<std::string>());
    if(!hotkey.Valid()||input::ReservedHotkey(hotkey)) result.values["laser"]["hotkey"]=defaults.values["laser"]["hotkey"];
    const auto drawingKey=input::ParseHotkey(result.values["drawing"]["hotkey"].get<std::string>());
    if(!drawingKey.Valid()||input::ReservedHotkey(drawingKey)||drawingKey.primary==input::Key::MouseLeft||
        drawingKey==input::ParseHotkey(result.values["laser"]["hotkey"].get<std::string>()))
        result.values["drawing"]["hotkey"]="Ctrl+Shift+Space";
    if(result.values["drawing"]["hotkey"]==result.values["laser"]["hotkey"])result.values["drawing"]["hotkey"]="F8";
    return result;
}
}
