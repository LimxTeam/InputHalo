#pragma once
#include <Windows.h>
#include <expected>
#include <string>
#include <utility>
#include "Core/Events.h"
#include <vector>
#include <functional>

namespace ih::platform {
class Win32Window {
public:
    Win32Window() = default;
    ~Win32Window();
    Win32Window(const Win32Window&) = delete;
    Win32Window& operator=(const Win32Window&) = delete;
    std::expected<void, std::string> Create(HINSTANCE instance);
    void Show(int command) const;
    void PumpMessages();
    HWND Handle() const { return m_window; }
    bool CloseRequested() const { return m_closeRequested; }
    bool NeedsRedraw() const { return m_needsRedraw; }
    void AcknowledgeRedraw() { m_needsRedraw = false; }
    SIZE ClientSize() const;
    bool ConsumeDisplayChange() { return std::exchange(m_displayChanged, false); }
    void SetTitle(const wchar_t* title) const { SetWindowTextW(m_window, title); }
    std::vector<UiEvent> TakeEvents() { return std::exchange(m_events, {}); }
    void RequestRedraw() { m_needsRedraw = true; }
    void SetTrayAvailable(bool available) { m_trayAvailable=available; }
    int ChromeHover() const { return m_chromeHover; }
    bool Maximized() const { return IsZoomed(m_window)!=FALSE; }
    void SetResizeRender(std::function<void()> callback){m_resizeRender=std::move(callback);}
private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    HINSTANCE m_instance{};
    HWND m_window{};
    ATOM m_class{};
    bool m_closeRequested{};
    bool m_needsRedraw = true;
    bool m_displayChanged{};
    std::vector<UiEvent> m_events;
    bool m_trayAvailable{};
    int m_chromeHover=-1;
    std::function<void()> m_resizeRender;
    bool m_resizing=false,m_rendering=false;
    void RenderDuringResize();
};
}
