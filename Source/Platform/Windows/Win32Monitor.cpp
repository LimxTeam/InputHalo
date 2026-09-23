/*******************************************************************************
 * 文件: Win32Monitor.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   实现显示器枚举：通过 EnumDisplayMonitors 回调收集所有监视器的句柄与边界矩形。
 *   算法复杂度：O(n)，n 为监视器数量。
 *   空间-时间权衡：一次枚举收集全部监视器信息，避免重复系统调用。
 *
 * 设计哲学:
 *   以 Win32 回调函数收集监视器信息，封装为 expected 返回，供 OverlayManager 为每屏创建窗口。
 *
 * 技术特性:
 *   - 监视器枚举：EnumDisplayMonitors 遍历所有显示设备
 *   - 回调收集：通过 MONITORINFO 获取监视器边界 rcMonitor
 *   - 异常安全：回调内 try/catch 防止向量分配异常穿透 Win32 回调边界
 *
 * 依赖关系:
 *   外部依赖：Win32 API（EnumDisplayMonitors、GetMonitorInfoW）
 *
 * 注意事项:
 *   回调函数 Collect 定义在匿名命名空间，仅本文件可见
 *
 ******************************************************************************/
#include "Platform/Windows/Win32Monitor.h"
// Win32 监视器管理命名空间
namespace ih::platform {
// 匿名命名空间：内部辅助函数
namespace {
// 监视器枚举回调：每块显示器调用一次
// 将监视器句柄与边界追加到调用者传入的向量中
BOOL CALLBACK Collect(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    // 初始化 MONITORINFO 结构（cbSize 必须设置）
    MONITORINFO info{sizeof(MONITORINFO)};
    // 查询监视器详细信息
    if (!GetMonitorInfoW(monitor, &info)) return FALSE;
    try {
        // 将监视器句柄与边界矩形追加到结果向量
        reinterpret_cast<std::vector<MonitorInfo>*>(data)->push_back({monitor, info.rcMonitor});
        return TRUE;
    } catch (...) { return FALSE; }
}
}
// 枚举系统所有显示监视器
// 返回监视器信息向量；失败或无监视器时返回错误
std::expected<std::vector<MonitorInfo>, std::string> EnumerateMonitors() {
    std::vector<MonitorInfo> monitors;
    // 调用 Win32 枚举 API，通过回调收集结果
    if (!EnumDisplayMonitors(nullptr, nullptr, Collect, reinterpret_cast<LPARAM>(&monitors)) || monitors.empty())
        return std::unexpected("Cannot enumerate display monitors");
    return monitors;
}
}
