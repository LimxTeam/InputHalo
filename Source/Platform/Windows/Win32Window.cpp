/*******************************************************************************
 * 文件: Win32Window.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   实现设置窗口：无边框自绘 Vulkan 窗口，处理 NC 命中测试（自定义标题栏/边框缩放）、resize 期间渲染、DPI 变更、鼠标/键盘事件路由。
 *   算法复杂度：消息处理 O(1)；事件队列上限 1024。
 *   空间-时间权衡：resize 期间用 16ms 定时器持续渲染以保证流畅；鼠标移动事件合并（连续 Move 覆盖）。
 *
 * 设计哲学:
 *   WS_POPUP 无边框窗口 + WM_NCCALCSIZE 占满客户区 + WM_NCHITTEST 自定义边框/标题栏命中，整窗由 Vulkan 绘制。
 *
 * 技术特性:
 *   - 无边框自绘：WM_NCPAINT 不绘制，整帧由 Vulkan 渲染
 *   - 自定义标题栏：右上角 138px 区域绘制最小化/最大化/关闭按钮，40px 高度为标题栏拖拽区
 *   - 边框缩放：6px DPI 缩放边距返回 HT* 命中，保留原生缩放行为
 *   - resize 渲染：WM_ENTERSIZEMOVE 启动 16ms 定时器持续回调渲染
 *   - 事件路由：鼠标/键盘/滚轮转为 UiEvent 入队，移动事件合并
 *   - DPI 感知：WM_DPICHANGED 按 lParam 新矩形调整窗口位置尺寸
 *
 * 依赖关系:
 *   内部依赖：ih::platform（MapKey、Resources.h）、ih::Core::Events（UiEvent）
 *   外部依赖：Win32 API（windowsx.h 宏 GET_X_LPARAM 等）
 *
 * 注意事项:
 *   WM_CLOSE 时若托盘可用则隐藏窗口而非销毁，HWND 须存活于 Vulkan surface 之后
 *   最大化时客户区对齐到显示器工作区（rcWork）
 *
 ******************************************************************************/
#include "Platform/Windows/Win32Window.h"
#include "Platform/Windows/Win32Input.h"
#include "Platform/Windows/Resources.h"
#include <windowsx.h>

// Win32 设置窗口命名空间
namespace ih::platform {
// 匿名命名空间：设置窗口类名
namespace { constexpr wchar_t kWindowClass[] = L"InputHalo.Settings"; }
// 析构：销毁窗口并注销窗口类
Win32Window::~Win32Window() {
    if (m_window) DestroyWindow(m_window);
    if (m_class) UnregisterClassW(kWindowClass, m_instance);
}
// 创建设置窗口：注册类、按系统 DPI 计算初始尺寸、创建无边框弹出窗口
std::expected<void, std::string> Win32Window::Create(HINSTANCE instance) {
    m_instance = instance;
    // 配置窗口类：自定义过程、箭头光标、应用图标
    WNDCLASSEXW definition{sizeof(WNDCLASSEXW)};
    definition.lpfnWndProc = WindowProc;
    definition.hInstance = instance;
    definition.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    // 加载大图标与小图标（任务栏/标题栏）
    definition.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_INPUTHALO));
    definition.hIconSm = static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(IDI_INPUTHALO),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED));
    definition.lpszClassName = kWindowClass;
    // 注册窗口类
    m_class = RegisterClassExW(&definition);
    if (!m_class) return std::unexpected("RegisterClassEx failed: " + std::to_string(GetLastError()));
    // 按系统 DPI 计算初始窗口尺寸（840x620 逻辑像素）
    const auto dpi = GetDpiForSystem();
    RECT bounds{0, 0, MulDiv(840, static_cast<int>(dpi), 96), MulDiv(620, static_cast<int>(dpi), 96)};
    // 注释保留：保留原生移动/缩放/最小化行为但不请求系统标题栏
    // Keep native move/resize/minimize behavior without requesting a system caption.
    // 窗口样式：弹出+可缩放+系统菜单+最小化+最大化按钮
    constexpr DWORD style=WS_POPUP|WS_THICKFRAME|WS_SYSMENU|WS_MINIMIZEBOX|WS_MAXIMIZEBOX;
    // 创建无边框窗口（WS_EX_APPWINDOW 显示在任务栏）
    m_window = CreateWindowExW(WS_EX_APPWINDOW, kWindowClass, L"InputHalo", style,
        CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
        nullptr, nullptr, instance, this);
    if (!m_window) return std::unexpected("CreateWindowEx failed: " + std::to_string(GetLastError()));
    // 触发框架变更（使 NCCALCSIZE 生效）
    SetWindowPos(m_window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
    return {};
}
// 显示/隐藏窗口
void Win32Window::Show(int command) const { ShowWindow(m_window, command); }
// resize 期间渲染回调
// 防止重入：仅在 resizing 且未渲染中时调用外部 resizeRender
void Win32Window::RenderDuringResize(){
    if(!m_resizing||m_rendering||!m_resizeRender)return;
    m_rendering=true;
    try {m_resizeRender();}catch(...){m_closeRequested=true;}
    m_rendering=false;
}
// 获取客户区尺寸（最小化或不可见时返回 0）
SIZE Win32Window::ClientSize() const {
    if (IsIconic(m_window) || !IsWindowVisible(m_window)) return {};
    RECT rect{};
    GetClientRect(m_window, &rect);
    return {rect.right - rect.left, rect.bottom - rect.top};
}
// 消息泵：PeekMessage 非阻塞取消息并分发
// 收到 WM_QUIT 时标记关闭并退出
void Win32Window::PumpMessages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) { m_closeRequested = true; return; }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
// 设置窗口过程：路由全部 Win32 消息
// 通过 GWLP_USERDATA 绑定实例，处理 NC 区域、命中测试、事件路由
LRESULT CALLBACK Win32Window::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<Win32Window*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    switch (message) {
    // NC 绘制：不绘制，整帧由 Vulkan 渲染
    case WM_NCPAINT: return 0; // The entire frame is painted by Vulkan.
    case WM_NCACTIVATE: return TRUE;
    // 进入移动/缩放循环：标记 resizing 并启动 16ms 渲染定时器
    case WM_ENTERSIZEMOVE:
        if(self){self->m_resizing=true;SetTimer(window,1,16,nullptr);}
        return 0;
    // 退出移动/缩放：停止定时器，触发一次渲染
    case WM_EXITSIZEMOVE:
        KillTimer(window,1);
        if(self){self->RenderDuringResize();self->m_resizing=false;self->m_needsRedraw=true;}
        return 0;
    // 定时器消息：resize 期间持续渲染
    case WM_TIMER:
        if(wParam==1&&self){self->RenderDuringResize();return 0;}
        break;
    // NC 计算：客户区占满整个窗口（无边框）
    // 最大化时对齐到显示器工作区，避开任务栏
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
    // NC 命中测试：自定义边框与标题栏拖拽区
    // 6px 边距返回缩放光标；40px 标题栏区返回 HTCAPTION
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
        ScreenToClient(window,&point);RECT client{};GetClientRect(window,&client);
        // 按窗口 DPI 缩放边距宽度
        const float scale=GetDpiForWindow(window)/96.f;
        const int edge=static_cast<int>(6*scale);
        // 非最大化时检测四边缩放区域
        if(!IsZoomed(window)) {
            const bool left=point.x<edge,right=point.x>=client.right-edge;
            const bool top=point.y<edge,bottom=point.y>=client.bottom-edge;
            if(top)return left?HTTOPLEFT:right?HTTOPRIGHT:HTTOP;
            if(bottom)return left?HTBOTTOMLEFT:right?HTBOTTOMRIGHT:HTBOTTOM;
            if(left)return HTLEFT;if(right)return HTRIGHT;
        }
        // 标题栏拖拽区（右上角按钮除外）返回 HTCAPTION
        if(point.y<40*scale && point.x<client.right-138*scale)return HTCAPTION;
        return HTCLIENT;
    }
    // 鼠标离开：清除按钮悬停态
    case WM_MOUSELEAVE:
        if(self){self->m_chromeHover=-1;self->m_needsRedraw=true;}
        return 0;
    // 最小窗口尺寸限制（720x520 逻辑像素，按 DPI 缩放）
    case WM_GETMINMAXINFO: {
        auto* info=reinterpret_cast<MINMAXINFO*>(lParam);
        const auto dpi=GetDpiForWindow(window);
        info->ptMinTrackSize={MulDiv(720,static_cast<int>(dpi),96),MulDiv(520,static_cast<int>(dpi),96)};
        return 0;
    }
    // 鼠标/键盘事件路由：转为 UiEvent 入队
    // 右上角 138px 区域为窗口控制按钮（最小化/最大化/关闭）
    case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_MOUSEWHEEL:
    case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_KEYUP: case WM_SYSKEYUP: case WM_KILLFOCUS: {
        if(self) try {
        // 计算 DPI 缩放与客户区
            const float scale=GetDpiForWindow(window)/96.f;
            RECT client{};GetClientRect(window,&client);
            const float x=static_cast<float>(GET_X_LPARAM(lParam)),y=static_cast<float>(GET_Y_LPARAM(lParam));
        // 检测鼠标是否在右上角按钮区（3 个按钮，每个 46px 宽）
            const int button=y>=0&&y<40*scale&&x>=client.right-138*scale ?
                static_cast<int>((x-(client.right-138*scale))/(46*scale)):-1;
        // 鼠标移动：更新按钮悬停态并跟踪离开事件
            if(message==WM_MOUSEMOVE){
                if(self->m_chromeHover!=button){self->m_chromeHover=button;self->m_needsRedraw=true;}
                TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window,0};TrackMouseEvent(&track);
            }
        // 按钮点击：最小化/最大化/关闭
            if(message==WM_LBUTTONDOWN&&button>=0){
                if(button==0)ShowWindow(window,SW_MINIMIZE);
                else if(button==1)ShowWindow(window,IsZoomed(window)?SW_RESTORE:SW_MAXIMIZE);
                else if(button==2)PostMessageW(window,WM_CLOSE,0,0);
                return 0;
            }
        // 构造 UiEvent 并填充坐标与类型
            UiEvent event{};
            event.x=static_cast<float>(GET_X_LPARAM(lParam));event.y=static_cast<float>(GET_Y_LPARAM(lParam));
            if(message==WM_MOUSEMOVE) event.type=UiEventType::Move;
            if(message==WM_LBUTTONDOWN) {event.type=UiEventType::Down;SetCapture(window);}
            if(message==WM_LBUTTONUP) {event.type=UiEventType::Up;ReleaseCapture();}
            // 滚轮：屏幕坐标转客户坐标，标准化增量
            if(message==WM_MOUSEWHEEL) {
                POINT p{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};ScreenToClient(window,&p);
                event.x=static_cast<float>(p.x);event.y=static_cast<float>(p.y);event.type=UiEventType::Wheel;
                event.wheel=GET_WHEEL_DELTA_WPARAM(wParam)/120.f;
            }
            // 键盘：映射虚拟键，记录 Shift 状态
            if(message==WM_KEYDOWN||message==WM_SYSKEYDOWN||message==WM_KEYUP||message==WM_SYSKEYUP) {
                event.type=(message==WM_KEYDOWN||message==WM_SYSKEYDOWN)?UiEventType::KeyDown:UiEventType::KeyUp;
                event.key=MapKey(static_cast<uint16_t>(wParam),static_cast<uint16_t>((lParam>>16)&255),(lParam&(1<<24))?RI_KEY_E0:0);
                event.shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
            }
            if(message==WM_KILLFOCUS) event.type=UiEventType::Blur;
        // 事件队列超限则清空
            if(self->m_events.size()>1024)self->m_events.clear();
        // 连续鼠标移动事件合并（覆盖最后一条），减少冗余
            if(event.type==UiEventType::Move&&!self->m_events.empty()&&self->m_events.back().type==UiEventType::Move)self->m_events.back()=event;
            else self->m_events.push_back(event);
            self->m_needsRedraw=true;
        }catch(...) {if(self)self->m_events.clear();}
        if(message==WM_SYSKEYDOWN||message==WM_SYSKEYUP||message==WM_KILLFOCUS) break;
        return 0;
    }
    // 显示器/DWM 变更：置位标志
    case WM_DISPLAYCHANGE:
    case WM_DWMCOMPOSITIONCHANGED:
        if (self) self->m_displayChanged = true;
        return 0;
    // 电源事件（休眠/唤醒）：标记需重绘
    case WM_POWERBROADCAST:
        if (self && (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND)) self->m_displayChanged = true;
        return TRUE;
    // WM_CLOSE：有托盘则隐藏窗口，否则最小化
    // 不销毁 HWND（Vulkan surface 依赖其存活）
    case WM_CLOSE:
        ShowWindow(window,self&&self->m_trayAvailable?SW_HIDE:SW_MINIMIZE);
        if(self){self->m_events.clear();self->m_events.push_back({UiEventType::Blur});}
        return 0; // HWND must outlive the Vulkan surface and swapchain.
    // 窗口尺寸变化：请求重绘并触发 resize 渲染
    case WM_SIZE:
        if (self) {self->m_needsRedraw = true;self->RenderDuringResize();}
        return 0;
    case WM_ERASEBKGND: return 1;
    // WM_PAINT：空 begin/end，标记需重绘
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(window, &paint);
        EndPaint(window, &paint);
        if (self) self->m_needsRedraw = true;
        if(self)self->RenderDuringResize();
        return 0;
    }
    // DPI 变更：按 lParam 新矩形调整窗口位置与尺寸
    case WM_DPICHANGED: {
        const auto* rect = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left,
            rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    // 窗口销毁：发送退出消息
    case WM_DESTROY: PostQuitMessage(0); return 0;
    // NC 销毁：清除实例指针与用户数据
    case WM_NCDESTROY:
        if (self) self->m_window = nullptr;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}
