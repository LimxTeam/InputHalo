/*******************************************************************************
 * 文件: Win32Window.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   声明设置窗口类 Win32Window：无边框 Vulkan 窗口的创建、消息泵、事件队列、渲染协调接口。
 *   不涉及复杂算法；接口声明。
 *   空间-时间权衡：事件向量按需交换（TakeEvents），移动事件合并。
 *
 * 设计哲学:
 *   封装 Win32 窗口生命周期与消息循环，对外暴露 UiEvent 队列与重绘请求，由渲染层驱动 Vulkan。
 *
 * 技术特性:
 *   - 不可拷贝：删除拷贝构造与赋值
 *   - 事件队列：TakeEvents 原子取出 UiEvent
 *   - 重绘协调：NeedsRedraw/AcknowledgeRedraw/RequestRedraw 三态协调渲染循环
 *   - resize 回调：SetResizeRender 注入缩放期间渲染函数
 *   - 托盘感知：有托盘时关闭=隐藏窗口
 *
 * 依赖关系:
 *   内部依赖：ih::Core::Events（UiEvent、UiEventType）
 *   外部依赖：Win32 API、C++ 标准库 <vector>/<functional>
 *
 * 注意事项:
 *   Win32Window 假设单线程消息泵
 *   m_needsRedraw 初始为 true，确保首帧渲染
 *
 ******************************************************************************/
#pragma once
#include <Windows.h>
#include <expected>
#include <string>
#include <utility>
#include "Core/Events.h"
#include <vector>
#include <functional>

// Win32 设置窗口命名空间
namespace ih::platform {
// 设置窗口类：无边框 Vulkan 渲染窗口
// 管理窗口创建、消息泵、UiEvent 队列与重绘协调
class Win32Window {
public:
    Win32Window() = default;
    ~Win32Window();
    Win32Window(const Win32Window&) = delete;
    Win32Window& operator=(const Win32Window&) = delete;
    // 创建窗口（传入实例句柄）
    std::expected<void, std::string> Create(HINSTANCE instance);
    // 显示窗口
    void Show(int command) const;
    // 消息泵（非阻塞 PeekMessage）
    void PumpMessages();
    // 获取原生 HWND
    HWND Handle() const { return m_window; }
    // 是否请求关闭（WM_QUIT 后）
    bool CloseRequested() const { return m_closeRequested; }
    // 是否需要重绘
    bool NeedsRedraw() const { return m_needsRedraw; }
    // 确认重绘完成（清除标志）
    void AcknowledgeRedraw() { m_needsRedraw = false; }
    // 客户区尺寸
    SIZE ClientSize() const;
    // 原子消费显示器变更标志
    bool ConsumeDisplayChange() { return std::exchange(m_displayChanged, false); }
    // 设置窗口标题
    void SetTitle(const wchar_t* title) const { SetWindowTextW(m_window, title); }
    // 原子取出 UI 事件队列
    std::vector<UiEvent> TakeEvents() { return std::exchange(m_events, {}); }
    // 请求重绘
    void RequestRedraw() { m_needsRedraw = true; }
    // 设置托盘是否可用（影响关闭行为）
    void SetTrayAvailable(bool available) { m_trayAvailable=available; }
    // 当前按钮悬停索引（-1=无）
    int ChromeHover() const { return m_chromeHover; }
    // 是否最大化
    bool Maximized() const { return IsZoomed(m_window)!=FALSE; }
    // 注入 resize 期间渲染回调
    void SetResizeRender(std::function<void()> callback){m_resizeRender=std::move(callback);}
private:
    // 静态窗口过程
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    // 实例句柄
    HINSTANCE m_instance{};
    // 窗口句柄
    HWND m_window{};
    // 窗口类 ATOM
    ATOM m_class{};
    // 关闭请求标志
    bool m_closeRequested{};
    // 重绘请求标志（首帧即需渲染）
    bool m_needsRedraw = true;
    // 显示器变更标志
    bool m_displayChanged{};
    // UI 事件队列
    std::vector<UiEvent> m_events;
    // 托盘可用性标志
    bool m_trayAvailable{};
    // 窗口按钮悬停索引
    int m_chromeHover=-1;
    // resize 渲染回调
    std::function<void()> m_resizeRender;
    // resizing 与 rendering 重入保护标志
    bool m_resizing=false,m_rendering=false;
    // resize 期间渲染实现
    void RenderDuringResize();
};
}
