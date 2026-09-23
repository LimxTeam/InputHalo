/*******************************************************************************
 * 文件: Keys.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   声明按键枚举 Key、按键计数、索引工具、修饰键/快捷键判定、标签与热键解析接口。
 *   不涉及复杂算法；纯枚举与接口声明。
 *   空间-时间权衡：Key 用 uint16_t 紧凑存储，Index() 为 constexpr O(1) 转换。
 *
 * 设计哲学:
 *   定义跨平台的逻辑按键枚举，屏蔽硬件差异；Hotkey 结构封装修饰键+主键组合，用于配置与匹配。
 *
 * 技术特性:
 *   - 按键枚举：覆盖字母、数字、功能键、修饰键、导航键、小键盘、鼠标按键
 *   - 索引工具：constexpr Index() 将枚举转为数组下标
 *   - 热键结构：封装 ctrl/shift/alt/win 修饰键与主键，支持有效性判断与相等比较
 *   - 接口声明：IsModifier、IsShortcutKey、Label、ParseHotkey、FormatHotkey、ReservedHotkey
 *
 * 依赖关系:
 *   内部依赖：ih::input 命名空间
 *   外部依赖：C++ 标准库 <cstdint>、<string>、<string_view>、<vector>
 *
 * 注意事项:
 *   Key::Count 作为枚举末尾哨兵，KeyCount 即按键总数
 *   鼠标按键从 MouseLeft 开始连续编号，便于用区间判断
 *
 ******************************************************************************/
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
// 按键定义命名空间
namespace ih::input {
// 逻辑按键枚举：跨平台统一的按键标识
// 按类别连续排列：字母->数字->功能键->修饰键->导航键->小键盘->符号键->鼠标键
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
// 按键总数：由枚举末尾 Count 哨兵推导
constexpr unsigned KeyCount = static_cast<unsigned>(Key::Count);
// constexpr 索引转换：将 Key 枚举转为无符号下标
constexpr unsigned Index(Key key) { return static_cast<unsigned>(key); }
// 判断是否为修饰键
bool IsModifier(Key key);
// 判断是否为快捷键功能键
bool IsShortcutKey(Key key);
// 将 Key 转为可读标签字符串
std::string Label(Key key);
// 热键结构：修饰键标志位 + 主键
// Valid() 要求主键存在且不是修饰键本身
struct Hotkey {
    bool ctrl{}, shift{}, alt{}, win{};
    Key primary = Key::Unknown;
    bool Valid() const { return primary != Key::Unknown && !IsModifier(primary); }
    bool operator==(const Hotkey&) const = default;
};
// 从文本解析热键
Hotkey ParseHotkey(std::string_view text);
// 将热键格式化为文本
std::string FormatHotkey(const Hotkey& hotkey);
// 判断是否为系统保留热键
bool ReservedHotkey(const Hotkey& hotkey);
}
