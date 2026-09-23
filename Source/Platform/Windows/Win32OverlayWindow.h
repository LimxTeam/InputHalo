/*******************************************************************************
 * 文件: Win32OverlayWindow.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   声明透明覆盖窗口类 Win32OverlayWindow。
 *   不涉及复杂算法；接口声明。
 *   空间-时间权衡：无额外缓冲，窗口本身即渲染目标。
 *
 * 设计哲学:
 *   封装 Win32 覆盖窗口的创建、显示/隐藏、交互切换、DPI 查询。
 *
 * 技术特性:
 *   - 不可拷贝：删除拷贝构造与赋值
 *   - DPI 感知：DpiScale() 按窗口 DPI 计算缩放比
 *   - 变更检测：ConsumeDisplayChange 原子消费变更标志
 *
 * 依赖关系:
 *   外部依赖：Win32 API（Windows.h）、C++ 标准库
 *
 * 注意事项:
 *   窗口为分层透明窗口，配合 DirectComposition 使用
 *
 ******************************************************************************/
#pragma once
#include <Windows.h>
#include <expected>
#include <string>
#include <utility>
// Win32 覆盖窗口命名空间
namespace ih::platform {
// 透明覆盖窗口类：全屏置顶、鼠标穿透
// 作为 DirectComposition 渲染目标的宿主 HWND
class Win32OverlayWindow {
public:
    Win32OverlayWindow() = default;
    ~Win32OverlayWindow();
    Win32OverlayWindow(const Win32OverlayWindow&) = delete;
    Win32OverlayWindow& operator=(const Win32OverlayWindow&) = delete;
    // 创建窗口（传入监视器边界）
    std::expected<void, std::string> Create(const RECT& bounds);
    // 显示窗口（不激活）
    void Show() const { ShowWindow(m_window, SW_SHOWNOACTIVATE); }
    // 隐藏窗口
    void Hide() const { ShowWindow(m_window, SW_HIDE); }
    // 获取原生 HWND
    HWND Handle() const { return m_window; }
    // 切换鼠标交互
    void SetInteractive(bool enabled);
    // DPI 缩放比（96 为基准 1.0）
    float DpiScale() const { return GetDpiForWindow(m_window) / 96.0f; }
    // 原子消费显示器变更标志
    bool ConsumeDisplayChange() { return std::exchange(m_displayChanged, false); }
private:
    // 静态窗口过程
    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
    // 窗口句柄
    HWND m_window{};
    // 显示器变更标志
    bool m_displayChanged{};
    // 交互模式标志（false=穿透）
    bool m_interactive{};
};
}
