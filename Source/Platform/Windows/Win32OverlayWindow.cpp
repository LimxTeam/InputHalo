/*******************************************************************************
 * 文件: Win32OverlayWindow.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   实现透明覆盖窗口：创建全屏置顶、鼠标穿透、无重定向位图的分层窗口，用于 Vulkan/DirectComposition 渲染。
 *   不涉及复杂算法；Win32 窗口配置与消息路由。
 *   空间-时间权衡：窗口类引用计数（g_windowCount）管理注册/注销时机。
 *
 * 设计哲学:
 *   覆盖窗口默认完全鼠标穿透（WS_EX_TRANSPARENT），可切换为交互模式；DComp 内容通过 NOREDIRECTIONBITMAP 直接合成。
 *
 * 技术特性:
 *   - 透明覆盖：WS_EX_TOPMOST|TOOLWINDOW|NOACTIVATE|TRANSPARENT|NOREDIRECTIONBITMAP|LAYERED
 *   - 鼠标穿透：默认 HTTRANSPARENT，SetInteractive 切换为 HTCLIENT
 *   - 延迟显示：首帧渲染前隐藏，避免闪烁
 *   - DPI/分辨率变更检测：WM_DISPLAYCHANGE/DPICHANGED 置位标志
 *
 * 依赖关系:
 *   外部依赖：Win32 API（CreateWindowEx、SetLayeredWindowAttributes）
 *
 * 注意事项:
 *   窗口类仅在 UI 线程注册，引用计数管理生命周期
 *   WM_CLOSE 被吞掉，应用完全控制覆盖窗口销毁时机
 *
 ******************************************************************************/
#include "Platform/Windows/Win32OverlayWindow.h"
// Win32 覆盖窗口命名空间
namespace ih::platform {
// 匿名命名空间：窗口类名与引用计数
namespace {
// 覆盖窗口类名（唯一标识）
constexpr wchar_t kClassName[] = L"InputHalo.Overlay";
// 窗口类引用计数：归零时注销类（窗口类仅限 UI 线程）
unsigned g_windowCount{}; // Window class ownership is confined to the UI thread.
}
// 析构：销毁窗口，引用计数归零时注销窗口类
Win32OverlayWindow::~Win32OverlayWindow() {
    if (m_window) {
        DestroyWindow(m_window);
        if (--g_windowCount == 0) UnregisterClassW(kClassName, GetModuleHandleW(nullptr));
    }
}
// 创建覆盖窗口
// bounds 为监视器物理像素边界；失败返回错误
std::expected<void, std::string> Win32OverlayWindow::Create(const RECT& bounds) {
    if (m_window) return std::unexpected("Overlay window already initialized");
    // 首个窗口时注册窗口类
    if (g_windowCount == 0) {
        WNDCLASSEXW definition{sizeof(WNDCLASSEXW)};
        definition.hInstance = GetModuleHandleW(nullptr);
        definition.lpfnWndProc = WindowProc;
        definition.lpszClassName = kClassName;
        if (!RegisterClassExW(&definition)) return std::unexpected("Cannot register overlay class");
    }
    // 窗口样式：置顶+工具窗+不激活+穿透+无重定向位图+分层
    constexpr DWORD styles = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT |
        WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED;
    // 创建弹出式窗口，尺寸匹配监视器边界
    m_window = CreateWindowExW(styles, kClassName, L"InputHalo Overlay", WS_POPUP,
        bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
        nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (!m_window) {
        if (g_windowCount == 0) UnregisterClassW(kClassName, GetModuleHandleW(nullptr));
        return std::unexpected("Cannot create overlay window");
    }
    // 窗口类引用计数递增
    ++g_windowCount;
    // 配置分层窗口不透明度（全不透明，Alpha 由 DComp 内容控制）
    if (!SetLayeredWindowAttributes(m_window, 0, 255, LWA_ALPHA)) return std::unexpected("Cannot configure overlay transparency");
    return {};
}
// 切换窗口鼠标交互模式
// enabled=true 时移除 WS_EX_TRANSPARENT，窗口可接收点击
void Win32OverlayWindow::SetInteractive(bool enabled){
    if(m_interactive==enabled)return;
    m_interactive=enabled;
    // 读取并修改扩展窗口样式
    auto style=GetWindowLongPtrW(m_window,GWL_EXSTYLE);
    SetWindowLongPtrW(m_window,GWL_EXSTYLE,enabled?(style&~WS_EX_TRANSPARENT):(style|WS_EX_TRANSPARENT));
    // 退出穿透时若有鼠标捕获则释放
    if(!enabled&&GetCapture()==m_window)ReleaseCapture();
}
// 窗口过程：路由覆盖窗口消息
LRESULT CALLBACK Win32OverlayWindow::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    // 取出实例指针
    auto* self = reinterpret_cast<Win32OverlayWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    // WM_NCCREATE 时绑定 this
    if (message == WM_NCCREATE) {
        self = static_cast<Win32OverlayWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    // 按消息类型处理
    switch (message) {
    // 命中测试：交互模式返回客户区，否则穿透到下层窗口
    case WM_NCHITTEST: return self&&self->m_interactive?HTCLIENT:HTTRANSPARENT;
    // 鼠标按下：交互模式时捕获鼠标
    case WM_LBUTTONDOWN: if(self&&self->m_interactive)SetCapture(window);return 0;
    // 鼠标释放：释放捕获
    case WM_LBUTTONUP: if(GetCapture()==window)ReleaseCapture();return 0;
    // 右键菜单：交互模式时吞掉默认菜单
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_CONTEXTMENU:
        if(self&&self->m_interactive)return 0;break;
    // 光标：交互模式显示十字光标
    case WM_SETCURSOR: if(self&&self->m_interactive){SetCursor(LoadCursorW(nullptr,IDC_CROSS));return TRUE;}break;
    // 鼠标激活：永不激活窗口
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    // 禁止背景擦除（内容由 DComp 渲染）
    case WM_ERASEBKGND: return 1;
    // WM_CLOSE 吞掉：应用完全管理覆盖窗口生命周期
    case WM_CLOSE: return 0; // Application owns the complete overlay lifecycle.
    // 显示器/DPI/DWM 变更：置位变更标志
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
    case WM_DWMCOMPOSITIONCHANGED:
        if (self) self->m_displayChanged = true;
        return 0;
    // 绘制：空 begin/end（无 GDI 绘制）
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
