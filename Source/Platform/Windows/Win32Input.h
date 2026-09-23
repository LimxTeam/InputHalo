#pragma once
#include <Windows.h>
#include "Input/InputState.h"
#include <expected>
#include <string>
#include <vector>
#include <utility>
#include <optional>
namespace ih::platform {
input::Key MapKey(uint16_t virtualKey, uint16_t scan, uint16_t flags);
class Win32Input {
public:
    ~Win32Input();
    std::expected<void, std::string> Initialize();
    std::vector<input::Event> TakeEvents() { return std::exchange(m_events, {}); }
    bool Failed() const { return m_failed; }
    static render2d::Point CursorPosition();
    static std::optional<render2d::Point> TryCursorPosition();
    static bool DesktopAvailable();
    static bool ReduceMotion();
private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    void Read(HRAWINPUT handle);
    HWND m_window{};
    std::vector<input::Event> m_events;
    bool m_failed{};
};
}
