/*******************************************************************************
 * 文件: Keys.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   实现按键标签映射、热键解析与格式化、保留热键判定。
 *   算法复杂度：Label 为 O(1) 区间判断+switch；ParseHotkey 为 O(n*m)（n 为 token 数，m 为键数），规模极小。
 *   空间-时间权衡：用 switch 硬编码标签表，避免查找表内存开销；按键数量固定约 100+，性能无忧。
 *
 * 设计哲学:
 *   将内部 Key 枚举与用户可读字符串互转；解析 Ctrl+Shift+A 形式的热键文本；识别系统保留热键。
 *
 * 技术特性:
 *   - 标签生成：将 Key 枚举转为可读标签（如 F1、Left click）
 *   - 热键解析：从文本解析出修饰键与主键组合
 *   - 热键格式化：将 Hotkey 结构反向序列化为字符串
 *   - 保留热键：识别 Ctrl+Alt+Del 等系统级快捷键，避免应用抢占
 *
 * 依赖关系:
 *   内部依赖：ih::input 命名空间（Keys.h）
 *   外部依赖：C++ 标准库 <array>（间接）、std::string/std::to_string
 *
 * 注意事项:
 *   IsShortcutKey 将 F1-F24、方向键、PrintScreen、Pause 视为快捷键（排除 Space）
 *   ParseHotkey 对格式错误返回空 Hotkey（primary=Unknown）
 *
 ******************************************************************************/
#include "Input/Keys.h"
#include <array>
// 按键映射与热键工具命名空间
namespace ih::input {
// 判断是否为修饰键（左右 Ctrl/Shift/Alt/Win 区间）
bool IsModifier(Key k) { return k >= Key::LeftCtrl && k <= Key::RightWin; }
// 判断是否为快捷键功能键
// 含 F1-F24、方向键/编辑键（排除 Space）、PrintScreen、Pause
bool IsShortcutKey(Key k) {
    return (k >= Key::F1 && k <= Key::F24) || (k >= Key::Escape && k <= Key::Down && k != Key::Space) ||
        k == Key::PrintScreen || k == Key::Pause;
}
// 将 Key 枚举转为人类可读标签字符串
std::string Label(Key k) {
    // 字母键 A-Z：直接映射为对应大写字母
    if (k >= Key::A && k <= Key::Z) return std::string(1, static_cast<char>('A' + Index(k) - Index(Key::A)));
    // 主键盘数字 0-9
    if (k >= Key::Num0 && k <= Key::Num9) return std::string(1, static_cast<char>('0' + Index(k) - Index(Key::Num0)));
    // 功能键 F1-F24
    if (k >= Key::F1 && k <= Key::F24) return "F" + std::to_string(Index(k) - Index(Key::F1) + 1);
    // 小键盘数字
    if (k >= Key::Numpad0 && k <= Key::Numpad9) return "Num " + std::to_string(Index(k) - Index(Key::Numpad0));
    // 其余按键通过 switch 逐一映射标签
    switch (k) {
    case Key::LeftCtrl: case Key::RightCtrl: return "Ctrl";
    case Key::LeftShift: case Key::RightShift: return "Shift";
    case Key::LeftAlt: case Key::RightAlt: return "Alt";
    case Key::LeftWin: case Key::RightWin: return "Win";
    case Key::Escape:return "Esc"; case Key::Tab:return "Tab"; case Key::Enter:return "Enter";
    case Key::NumpadEnter:return "Num Enter"; case Key::Space:return "Space"; case Key::Backspace:return "Backspace";
    case Key::Delete:return "Delete"; case Key::Insert:return "Insert"; case Key::Home:return "Home"; case Key::End:return "End";
    case Key::PageUp:return "Page Up"; case Key::PageDown:return "Page Down";
    case Key::Left:return "Left"; case Key::Right:return "Right"; case Key::Up:return "Up"; case Key::Down:return "Down";
    case Key::CapsLock:return "Caps Lock"; case Key::NumLock:return "Num Lock"; case Key::ScrollLock:return "Scroll Lock";
    case Key::PrintScreen:return "Print Screen"; case Key::Pause:return "Pause"; case Key::Menu:return "Menu";
    case Key::Add:return "Num Plus"; case Key::Subtract:return "Num -"; case Key::Multiply:return "Num *";
    case Key::Divide:return "Num /"; case Key::Decimal:return "Num .";
    case Key::Semicolon:return ";"; case Key::Apostrophe:return "'"; case Key::Grave:return "`";
    case Key::Minus:return "-"; case Key::Equals:return "="; case Key::LeftBracket:return "[";
    case Key::RightBracket:return "]"; case Key::Backslash:return "\\"; case Key::Comma:return ",";
    case Key::Period:return "."; case Key::Slash:return "/";
    case Key::MouseLeft:return "Left click"; case Key::MouseRight:return "Right click"; case Key::MouseMiddle:return "Middle click";
    case Key::Mouse4:return "Mouse4"; case Key::Mouse5:return "Mouse5";
    default:return "Unknown";
    }
}
// 解析热键文本（如 Ctrl+Shift+A）为 Hotkey 结构
// 格式错误或主键重复时返回空 Hotkey
Hotkey ParseHotkey(std::string_view text) {
    Hotkey result;
    // 按 '+' 分割 token 并去除首尾空格
    while (!text.empty()) {
        const auto split = text.find('+');
        auto token = text.substr(0, split);
        while (!token.empty() && token.front() == ' ') token.remove_prefix(1);
        while (!token.empty() && token.back() == ' ') token.remove_suffix(1);
        // 识别修饰键 token
        if (token == "Ctrl") result.ctrl = true;
        else if (token == "Shift") result.shift = true;
        else if (token == "Alt") result.alt = true;
        else if (token == "Win") result.win = true;
        // 非修饰键：作为主键，遍历查找匹配标签的键
        else {
            if (result.primary != Key::Unknown) return {};
            for (unsigned i = 1; i < KeyCount; ++i) {
                auto key = static_cast<Key>(i);
                if (!IsModifier(key) && Label(key) == token) { result.primary = key; break; }
            }
            if (result.primary == Key::Unknown) return {};
        }
        if (split == std::string_view::npos) break;
        text.remove_prefix(split + 1);
        if (text.empty()) return {};
    }
    return result;
}
// 将 Hotkey 格式化为可读字符串（如 Ctrl+Shift+A）
// 按 Ctrl/Shift/Alt/Win 顺序拼接修饰键，最后接主键标签
std::string FormatHotkey(const Hotkey& hotkey) {
    std::string result;
    if (hotkey.ctrl) result += "Ctrl+";
    if (hotkey.shift) result += "Shift+";
    if (hotkey.alt) result += "Alt+";
    if (hotkey.win) result += "Win+";
    return result + Label(hotkey.primary);
}
// 判断热键是否为系统保留组合（应用不应拦截）
// 覆盖 Ctrl+Alt+Del、Win+L、Alt+F4、Alt+Tab、Ctrl+Shift+Esc
bool ReservedHotkey(const Hotkey& k) {
    return (k.ctrl && k.alt && k.primary == Key::Delete) || (k.win && k.primary == Key::L) ||
        (k.alt && k.primary == Key::F4) || (k.alt && k.primary == Key::Tab) || (k.ctrl && k.shift && k.primary == Key::Escape);
}
}
