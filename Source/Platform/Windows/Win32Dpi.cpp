/*******************************************************************************
 * 文件: Win32Dpi.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   实现进程级 DPI 感知初始化：启用 Per-Monitor V2 DPI 感知，确保覆盖窗口在高 DPI 显示器上正确缩放。
 *   不涉及复杂算法；单次系统调用。
 *   不涉及空间-时间权衡。
 *
 * 设计哲学:
 *   在程序启动时尽早调用，使窗口按真实像素渲染而非被系统位图拉伸。
 *
 * 技术特性:
 *   - Per-Monitor V2 感知：支持每块显示器独立 DPI 缩放
 *   - 降级检查：若设置失败则验证当前线程 DPI 上下文是否已为目标值
 *
 * 依赖关系:
 *   外部依赖：Win32 API（SetProcessDpiAwarenessContext、AreDpiAwarenessContextsEqual）
 *
 * 注意事项:
 *   必须在创建任何窗口之前调用，否则 DPI 感知不生效
 *   Per-Monitor V2 需要 Windows 10 1607+
 *
 ******************************************************************************/
#include "Platform/Windows/Win32Dpi.h"
#include <Windows.h>
// Win32 DPI 适配命名空间
namespace ih::platform {
// 初始化进程 DPI 感知为 Per-Monitor V2
// 成功返回 void；失败返回错误描述（含 Win32 错误码）
std::expected<void, std::string> InitializeDpi() {
    // 优先尝试设置 Per-Monitor V2 DPI 感知上下文
    if (SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return {};
    // 若首次调用失败（可能已被 manifest 声明），验证当前上下文是否匹配
    if (AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(),DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return {};
    // 仍不匹配则返回错误
    return std::unexpected("Cannot enable per-monitor V2 DPI: " + std::to_string(GetLastError()));
}
}
