#pragma once
#include <Windows.h>
#include <expected>
#include <string>
#include <utility>
namespace ih::platform {
class Win32OverlayWindow {
public:
    Win32OverlayWindow() = default;
    ~Win32OverlayWindow();
    Win32OverlayWindow(const Win32OverlayWindow&) = delete;
    Win32OverlayWindow& operator=(const Win32OverlayWindow&) = delete;
    std::expected<void, std::string> Create(const RECT& bounds);
    void Show() const { ShowWindow(m_window, SW_SHOWNOACTIVATE); }
    void Hide() const { ShowWindow(m_window, SW_HIDE); }
    HWND Handle() const { return m_window; }
    void SetInteractive(bool enabled);
    float DpiScale() const { return GetDpiForWindow(m_window) / 96.0f; }
    bool ConsumeDisplayChange() { return std::exchange(m_displayChanged, false); }
private:
    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
    HWND m_window{};
    bool m_displayChanged{};
    bool m_interactive{};
};
}
