/*******************************************************************************
 * 文件: SettingsModel.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   实现默认配置基线 Defaults() 与防御性加载 FromJson()：类型合并、数值区间钳制、
 *   字符串白名单、排除项清洗、颜色格式校验、快捷键合法性与冲突检测。
 *   复杂度: O(默认字段数) 的一次性合并与校验，空间为单份配置树。
 *
 * 设计哲学:
 *   防御性加载哲学：任何非法输入都回退默认值，绝不把脏数据交给渲染层；
 *   所有取值约束集中于此处一处，UI 与渲染可无条件信任 SettingsModel。
 *
 * 技术特性:
 *   - 数值用 clamp lambda 钳到合法区间，非有限数回退下限
 *   - 字符串用 choice lambda 限定到枚举白名单
 *   - 颜色强制 #RRGGBB 十六进制格式
 *   - 排除项去重、限长、限数(≤128)并校验 .exe / 热键合法
 *
 * 依赖关系:
 *   内部: Input/Keys.h(热键解析 ParseHotkey/ReservedHotkey)
 *   外部: nlohmann/json、<algorithm>、<cmath>、<stdexcept>
 *
 * 注意事项:
 *   仅允许 number↔number 宽松匹配，其余要求严格类型相等；非法键被丢弃。
 ******************************************************************************/
#include "Settings/SettingsModel.h"
#include "Input/Keys.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>

// InputHalo 配置层命名空间实现。
namespace ih {
// 默认配置基线：以原始 JSON 字符串给出全部分区的默认值。
SettingsModel SettingsModel::Defaults() {
// general: 启动/语言/显示器/主题色；input: 键盘鼠标提示；
// cursor: 光标高亮；drawing: 标注；exclusions: 排除；laser: 激光笔。
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

// 安全加载：以默认值为模板，逐分区逐键覆盖，再做统一校验。
SettingsModel SettingsModel::FromJson(const nlohmann::json& source) {
// 根节点必须是对象。
    if (!source.is_object()) throw std::runtime_error("Settings root must be an object");
// 显式版本号不为 1 时直接拒绝，避免错误版本被静默接受。
    if (source.contains("version") && source["version"] != 1)
        throw std::runtime_error("Unsupported settings version");
// 从默认值开始合并。
    auto result = Defaults();
// 遍历默认模板的每个分区。
    for (auto& [section, fields] : result.values.items()) {
// 源缺分区或类型不对则保留默认。
        if (!fields.is_object() || !source.contains(section) || !source[section].is_object()) continue;
// 遍历模板中的每个键。
        for (auto& [key, value] : fields.items()) {
            const auto& object = source[section];
// 源缺键则保留默认。
            if (!object.contains(key)) continue;
            const auto& candidate = object[key];
// 仅当类型一致(或均为数字)时才覆盖。
            if ((value.is_number() && candidate.is_number()) || value.type() == candidate.type())
                value = candidate;
        }
    }
// 数值钳制 lambda：把 section/key 钳到 [low,high]，非有限数回退 low。
    auto clamp = [&](const char* section, const char* key, double low, double high) {
        auto& value = result.values[section][key];
        const auto number = value.get<double>();
        value = std::isfinite(number) ? std::clamp(number, low, high) : low;
    };

// 逐字段配置合法区间：缩放、透明度、偏移、时间等。
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
// 取一份默认值作为枚举/颜色的回退来源。
    const auto defaults = Defaults();
// 字符串白名单 lambda：不在 allowed 内则回退默认。
    auto choice = [&](const char* section, const char* key, std::initializer_list<std::string_view> allowed) {
        const auto value = result.values[section][key].get<std::string>();
        if (std::find(allowed.begin(), allowed.end(), value) == allowed.end())
            result.values[section][key] = defaults.values[section][key];
    };
// 枚举约束：光标样式、语言、激光模式、标注形状。
    choice("cursor", "style", {"ring", "halo", "spot"});
    choice("general", "language", {"zh-CN", "en"});
    choice("laser", "mode", {"trail", "pointer"});
    choice("drawing", "shape", {"rectangle", "arrow", "ellipse"});
// 清洗排除列表(apps/hotkeys)：过滤非法条目并去重。
    for(const char* field:{"apps","hotkeys"}){
        auto clean=nlohmann::json::array();
// 遍历现有排除项。
        for(const auto& item:result.values["exclusions"][field]){
// 必须是字符串且数量不超过 128。
            if(!item.is_string()||clean.size()>=128)continue;
            auto value=item.get<std::string>();
// 拒绝空串、超长(>260)及含控制字符的条目。
            if(value.empty()||value.size()>260||value.find_first_of("\r\n\t")!=std::string::npos)continue;
// 快捷键类排除项：校验可解析且为主键(非鼠标)，规范化为标准写法。
            if(std::string_view(field)=="hotkeys"){
                const auto key=input::ParseHotkey(value);
                if(!key.Valid()||key.primary>=input::Key::MouseLeft)continue;
                value=input::FormatHotkey(key);
// 应用类排除项：过滤非法文件名字符，统一小写，必须以 .exe 结尾。
            }else{
                if(value.find_first_of("/\\:*?\"<>|")!=std::string::npos)continue;
                std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return c>='A'&&c<='Z'?static_cast<char>(c+32):static_cast<char>(c);});
                if(!value.ends_with(".exe"))continue;
            }
// 去重后追加。
            if(std::find(clean.begin(),clean.end(),value)==clean.end())clean.push_back(value);
        }
        result.values["exclusions"][field]=std::move(clean);
    }
// 锚点位置与覆盖显示器枚举约束。
    choice("input", "anchor", {"bottom_left", "bottom_center", "bottom_right", "top_left", "top_center", "top_right"});
    choice("general", "overlay_monitor", {"follow_cursor", "primary", "monitor_1", "monitor_2", "monitor_3", "monitor_4", "monitor_5", "monitor_6", "monitor_7", "monitor_8"});
// 颜色校验：general.accent 与各分区 color 必须为 #RRGGBB。
    for (const auto section : {"general", "cursor", "laser", "drawing"}) {
        const auto key = std::string_view(section) == "general" ? "accent" : "color";
        const auto color = result.values[section][key].get<std::string>();
// 长度=7、# 开头、其余为十六进制，否则回退默认色。
        if (color.size() != 7 || color.front() != '#' ||
            color.find_first_not_of("0123456789abcdefABCDEF", 1) != std::string::npos)
            result.values[section][key] = defaults.values[section][key];
    }
// 光标左/右键波纹颜色同样校验。
    for (const auto key : {"left_color", "right_color"}) {
        const auto color = result.values["cursor"][key].get<std::string>();
        if (color.size() != 7 || color.front() != '#' || color.find_first_not_of("0123456789abcdefABCDEF", 1) != std::string::npos)
            result.values["cursor"][key] = defaults.values["cursor"][key];
    }
// 激光快捷键校验：非法或被 Windows 保留则回退默认。
    const auto hotkey=input::ParseHotkey(result.values["laser"]["hotkey"].get<std::string>());
    if(!hotkey.Valid()||input::ReservedHotkey(hotkey)) result.values["laser"]["hotkey"]=defaults.values["laser"]["hotkey"];
// 标注快捷键校验：非法/保留/鼠标左键/与激光冲突时回退。
    const auto drawingKey=input::ParseHotkey(result.values["drawing"]["hotkey"].get<std::string>());
    if(!drawingKey.Valid()||input::ReservedHotkey(drawingKey)||drawingKey.primary==input::Key::MouseLeft||
        drawingKey==input::ParseHotkey(result.values["laser"]["hotkey"].get<std::string>()))
        result.values["drawing"]["hotkey"]="Ctrl+Shift+Space";
// 最后兜底：若仍与激光快捷键撞车，改用 F8。
    if(result.values["drawing"]["hotkey"]==result.values["laser"]["hotkey"])result.values["drawing"]["hotkey"]="F8";
    return result;
}
}
