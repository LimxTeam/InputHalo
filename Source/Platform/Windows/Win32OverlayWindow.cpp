#include "Platform/Windows/Win32OverlayWindow.h"
namespace ih::platform {
namespace {
constexpr wchar_t kClassName[] = L"InputHalo.Overlay";
unsigned g_windowCount{}; // Window class ownership is confined to the UI thread.
}
Win32OverlayWindow::~Win32OverlayWindow() {
    if (m_window) {
        DestroyWindow(m_window);
        if (--g_windowCount == 0) UnregisterClassW(kClassName, GetModuleHandleW(nullptr));
    }
}
std::expected<void, std::string> Win32OverlayWindow::Create(const RECT& bounds) {
    if (m_window) return std::unexpected("Overlay window already initialized");
    if (g_windowCount == 0) {
        WNDCLASSEXW definition{sizeof(WNDCLASSEXW)};
        definition.hInstance = GetModuleHandleW(nullptr);
        definition.lpfnWndProc = WindowProc;
        definition.lpszClassName = kClassName;
        if (!RegisterClassExW(&definition)) return std::unexpected("Cannot register overlay class");
    }
    constexpr DWORD styles = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT |
        WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED;
    m_window = CreateWindowExW(styles, kClassName, L"InputHalo Overlay", WS_POPUP,
        bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
        nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (!m_window) {
        if (g_windowCount == 0) UnregisterClassW(kClassName, GetModuleHandleW(nullptr));
        return std::unexpected("Cannot create overlay window");
    }
    ++g_windowCount;
    if (!SetLayeredWindowAttributes(m_window, 0, 255, LWA_ALPHA)) return std::unexpected("Cannot configure overlay transparency");
    return {};
}
void Win32OverlayWindow::SetInteractive(bool enabled){
    if(m_interactive==enabled)return;
    m_interactive=enabled;
    auto style=GetWindowLongPtrW(m_window,GWL_EXSTYLE);
    SetWindowLongPtrW(m_window,GWL_EXSTYLE,enabled?(style&~WS_EX_TRANSPARENT):(style|WS_EX_TRANSPARENT));
    if(!enabled&&GetCapture()==m_window)ReleaseCapture();
}
LRESULT CALLBACK Win32OverlayWindow::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<Win32OverlayWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<Win32OverlayWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    switch (message) {
    case WM_NCHITTEST: return self&&self->m_interactive?HTCLIENT:HTTRANSPARENT;
    case WM_LBUTTONDOWN: if(self&&self->m_interactive)SetCapture(window);return 0;
    case WM_LBUTTONUP: if(GetCapture()==window)ReleaseCapture();return 0;
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_CONTEXTMENU:
        if(self&&self->m_interactive)return 0;break;
    case WM_SETCURSOR: if(self&&self->m_interactive){SetCursor(LoadCursorW(nullptr,IDC_CROSS));return TRUE;}break;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_CLOSE: return 0; // Application owns the complete overlay lifecycle.
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
    case WM_DWMCOMPOSITIONCHANGED:
        if (self) self->m_displayChanged = true;
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(window, &paint);
        EndPaint(window, &paint);
        return 0;
    }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}
