#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
namespace ih::input {
enum class Key : uint16_t {
    Unknown, A,B,C,D,E,F,G,H,I,J,K,L,M,N,O,P,Q,R,S,T,U,V,W,X,Y,Z,
    Num0,Num1,Num2,Num3,Num4,Num5,Num6,Num7,Num8,Num9,
    F1,F2,F3,F4,F5,F6,F7,F8,F9,F10,F11,F12,F13,F14,F15,F16,F17,F18,F19,F20,F21,F22,F23,F24,
    LeftCtrl,RightCtrl,LeftShift,RightShift,LeftAlt,RightAlt,LeftWin,RightWin,
    Escape,Tab,Enter,NumpadEnter,Space,Backspace,Delete,Insert,Home,End,PageUp,PageDown,Left,Right,Up,Down,
    CapsLock,NumLock,ScrollLock,PrintScreen,Pause,Menu,
    Numpad0,Numpad1,Numpad2,Numpad3,Numpad4,Numpad5,Numpad6,Numpad7,Numpad8,Numpad9,
    Add,Subtract,Multiply,Divide,Decimal,
    Semicolon,Apostrophe,Grave,Minus,Equals,LeftBracket,RightBracket,Backslash,Comma,Period,Slash,
    MouseLeft,MouseRight,MouseMiddle,Mouse4,Mouse5,Count
};
constexpr unsigned KeyCount = static_cast<unsigned>(Key::Count);
constexpr unsigned Index(Key key) { return static_cast<unsigned>(key); }
bool IsModifier(Key key);
bool IsShortcutKey(Key key);
std::string Label(Key key);
struct Hotkey {
    bool ctrl{}, shift{}, alt{}, win{};
    Key primary = Key::Unknown;
    bool Valid() const { return primary != Key::Unknown && !IsModifier(primary); }
    bool operator==(const Hotkey&) const = default;
};
Hotkey ParseHotkey(std::string_view text);
std::string FormatHotkey(const Hotkey& hotkey);
bool ReservedHotkey(const Hotkey& hotkey);
}
