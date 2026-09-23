#include "Input/Keys.h"
#include <array>
namespace ih::input {
bool IsModifier(Key k) { return k >= Key::LeftCtrl && k <= Key::RightWin; }
bool IsShortcutKey(Key k) {
    return (k >= Key::F1 && k <= Key::F24) || (k >= Key::Escape && k <= Key::Down && k != Key::Space) ||
        k == Key::PrintScreen || k == Key::Pause;
}
std::string Label(Key k) {
    if (k >= Key::A && k <= Key::Z) return std::string(1, static_cast<char>('A' + Index(k) - Index(Key::A)));
    if (k >= Key::Num0 && k <= Key::Num9) return std::string(1, static_cast<char>('0' + Index(k) - Index(Key::Num0)));
    if (k >= Key::F1 && k <= Key::F24) return "F" + std::to_string(Index(k) - Index(Key::F1) + 1);
    if (k >= Key::Numpad0 && k <= Key::Numpad9) return "Num " + std::to_string(Index(k) - Index(Key::Numpad0));
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
Hotkey ParseHotkey(std::string_view text) {
    Hotkey result;
    while (!text.empty()) {
        const auto split = text.find('+');
        auto token = text.substr(0, split);
        while (!token.empty() && token.front() == ' ') token.remove_prefix(1);
        while (!token.empty() && token.back() == ' ') token.remove_suffix(1);
        if (token == "Ctrl") result.ctrl = true;
        else if (token == "Shift") result.shift = true;
        else if (token == "Alt") result.alt = true;
        else if (token == "Win") result.win = true;
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
std::string FormatHotkey(const Hotkey& hotkey) {
    std::string result;
    if (hotkey.ctrl) result += "Ctrl+";
    if (hotkey.shift) result += "Shift+";
    if (hotkey.alt) result += "Alt+";
    if (hotkey.win) result += "Win+";
    return result + Label(hotkey.primary);
}
bool ReservedHotkey(const Hotkey& k) {
    return (k.ctrl && k.alt && k.primary == Key::Delete) || (k.win && k.primary == Key::L) ||
        (k.alt && k.primary == Key::F4) || (k.alt && k.primary == Key::Tab) || (k.ctrl && k.shift && k.primary == Key::Escape);
}
}
