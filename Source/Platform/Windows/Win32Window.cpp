#include "Platform/Windows/Win32Window.h"
#include "Platform/Windows/Win32Input.h"
#include "Platform/Windows/Resources.h"
#include <windowsx.h>

namespace ih::platform {
namespace { constexpr wchar_t kWindowClass[] = L"InputHalo.Settings"; }
Win32Window::~Win32Window() {
    if (m_window) DestroyWindow(m_window);
    if (m_class) UnregisterClassW(kWindowClass, m_instance);
}
std::expected<void, std::string> Win32Window::Create(HINSTANCE instance) {
    m_instance = instance;
    WNDCLASSEXW definition{sizeof(WNDCLASSEXW)};
    definition.lpfnWndProc = WindowProc;
    definition.hInstance = instance;
    definition.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    definition.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_INPUTHALO));
    definition.hIconSm = static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(IDI_INPUTHALO),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED));
    definition.lpszClassName = kWindowClass;
    m_class = RegisterClassExW(&definition);
    if (!m_class) return std::unexpected("RegisterClassEx failed: " + std::to_string(GetLastError()));
    const auto dpi = GetDpiForSystem();
    RECT bounds{0, 0, MulDiv(840, static_cast<int>(dpi), 96), MulDiv(620, static_cast<int>(dpi), 96)};
    // Keep native move/resize/minimize behavior without requesting a system caption.
    constexpr DWORD style=WS_POPUP|WS_THICKFRAME|WS_SYSMENU|WS_MINIMIZEBOX|WS_MAXIMIZEBOX;
    m_window = CreateWindowExW(WS_EX_APPWINDOW, kWindowClass, L"InputHalo", style,
        CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
        nullptr, nullptr, instance, this);
    if (!m_window) return std::unexpected("CreateWindowEx failed: " + std::to_string(GetLastError()));
    SetWindowPos(m_window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
    return {};
}
void Win32Window::Show(int command) const { ShowWindow(m_window, command); }
void Win32Window::RenderDuringResize(){
    if(!m_resizing||m_rendering||!m_resizeRender)return;
    m_rendering=true;
    try {m_resizeRender();}catch(...){m_closeRequested=true;}
    m_rendering=false;
}
SIZE Win32Window::ClientSize() const {
    if (IsIconic(m_window) || !IsWindowVisible(m_window)) return {};
    RECT rect{};
    GetClientRect(m_window, &rect);
    return {rect.right - rect.left, rect.bottom - rect.top};
}
void Win32Window::PumpMessages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) { m_closeRequested = true; return; }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
LRESULT CALLBACK Win32Window::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<Win32Window*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    switch (message) {
    case WM_NCPAINT: return 0; // The entire frame is painted by Vulkan.
    case WM_NCACTIVATE: return TRUE;
    case WM_ENTERSIZEMOVE:
        if(self){self->m_resizing=true;SetTimer(window,1,16,nullptr);}
        return 0;
    case WM_EXITSIZEMOVE:
        KillTimer(window,1);
        if(self){self->RenderDuringResize();self->m_resizing=false;self->m_needsRedraw=true;}
        return 0;
    case WM_TIMER:
        if(wParam==1&&self){self->RenderDuringResize();return 0;}
        break;
    case WM_NCCALCSIZE:
        if(wParam) {
            auto* area=reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
            if(IsZoomed(window)) {
                MONITORINFO monitor{sizeof(monitor)};
                if(GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor))area->rgrc[0]=monitor.rcWork;
            }
            return 0;
        }
        return 0; // Also use the entire window during initial (wParam == FALSE) layout.
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
        ScreenToClient(window,&point);RECT client{};GetClientRect(window,&client);
        const float scale=GetDpiForWindow(window)/96.f;
        const int edge=static_cast<int>(6*scale);
        if(!IsZoomed(window)) {
            const bool left=point.x<edge,right=point.x>=client.right-edge;
            const bool top=point.y<edge,bottom=point.y>=client.bottom-edge;
            if(top)return left?HTTOPLEFT:right?HTTOPRIGHT:HTTOP;
            if(bottom)return left?HTBOTTOMLEFT:right?HTBOTTOMRIGHT:HTBOTTOM;
            if(left)return HTLEFT;if(right)return HTRIGHT;
        }
        if(point.y<40*scale && point.x<client.right-138*scale)return HTCAPTION;
        return HTCLIENT;
    }
    case WM_MOUSELEAVE:
        if(self){self->m_chromeHover=-1;self->m_needsRedraw=true;}
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info=reinterpret_cast<MINMAXINFO*>(lParam);
        const auto dpi=GetDpiForWindow(window);
        info->ptMinTrackSize={MulDiv(720,static_cast<int>(dpi),96),MulDiv(520,static_cast<int>(dpi),96)};
        return 0;
    }
    case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_MOUSEWHEEL:
    case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_KEYUP: case WM_SYSKEYUP: case WM_KILLFOCUS: {
        if(self) try {
            const float scale=GetDpiForWindow(window)/96.f;
            RECT client{};GetClientRect(window,&client);
            const float x=static_cast<float>(GET_X_LPARAM(lParam)),y=static_cast<float>(GET_Y_LPARAM(lParam));
            const int button=y>=0&&y<40*scale&&x>=client.right-138*scale ?
                static_cast<int>((x-(client.right-138*scale))/(46*scale)):-1;
            if(message==WM_MOUSEMOVE){
                if(self->m_chromeHover!=button){self->m_chromeHover=button;self->m_needsRedraw=true;}
                TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window,0};TrackMouseEvent(&track);
            }
            if(message==WM_LBUTTONDOWN&&button>=0){
                if(button==0)ShowWindow(window,SW_MINIMIZE);
                else if(button==1)ShowWindow(window,IsZoomed(window)?SW_RESTORE:SW_MAXIMIZE);
                else if(button==2)PostMessageW(window,WM_CLOSE,0,0);
                return 0;
            }
            UiEvent event{};
            event.x=static_cast<float>(GET_X_LPARAM(lParam));event.y=static_cast<float>(GET_Y_LPARAM(lParam));
            if(message==WM_MOUSEMOVE) event.type=UiEventType::Move;
            if(message==WM_LBUTTONDOWN) {event.type=UiEventType::Down;SetCapture(window);}
            if(message==WM_LBUTTONUP) {event.type=UiEventType::Up;ReleaseCapture();}
            if(message==WM_MOUSEWHEEL) {
                POINT p{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};ScreenToClient(window,&p);
                event.x=static_cast<float>(p.x);event.y=static_cast<float>(p.y);event.type=UiEventType::Wheel;
                event.wheel=GET_WHEEL_DELTA_WPARAM(wParam)/120.f;
            }
            if(message==WM_KEYDOWN||message==WM_SYSKEYDOWN||message==WM_KEYUP||message==WM_SYSKEYUP) {
                event.type=(message==WM_KEYDOWN||message==WM_SYSKEYDOWN)?UiEventType::KeyDown:UiEventType::KeyUp;
                event.key=MapKey(static_cast<uint16_t>(wParam),static_cast<uint16_t>((lParam>>16)&255),(lParam&(1<<24))?RI_KEY_E0:0);
                event.shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
            }
            if(message==WM_KILLFOCUS) event.type=UiEventType::Blur;
            if(self->m_events.size()>1024)self->m_events.clear();
            if(event.type==UiEventType::Move&&!self->m_events.empty()&&self->m_events.back().type==UiEventType::Move)self->m_events.back()=event;
            else self->m_events.push_back(event);
            self->m_needsRedraw=true;
        }catch(...) {if(self)self->m_events.clear();}
        if(message==WM_SYSKEYDOWN||message==WM_SYSKEYUP||message==WM_KILLFOCUS) break;
        return 0;
    }
    case WM_DISPLAYCHANGE:
    case WM_DWMCOMPOSITIONCHANGED:
        if (self) self->m_displayChanged = true;
        return 0;
    case WM_POWERBROADCAST:
        if (self && (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND)) self->m_displayChanged = true;
        return TRUE;
    case WM_CLOSE:
        ShowWindow(window,self&&self->m_trayAvailable?SW_HIDE:SW_MINIMIZE);
        if(self){self->m_events.clear();self->m_events.push_back({UiEventType::Blur});}
        return 0; // HWND must outlive the Vulkan surface and swapchain.
    case WM_SIZE:
        if (self) {self->m_needsRedraw = true;self->RenderDuringResize();}
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(window, &paint);
        EndPaint(window, &paint);
        if (self) self->m_needsRedraw = true;
        if(self)self->RenderDuringResize();
        return 0;
    }
    case WM_DPICHANGED: {
        const auto* rect = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left,
            rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_NCDESTROY:
        if (self) self->m_window = nullptr;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}
