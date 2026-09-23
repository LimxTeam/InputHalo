/*******************************************************************************
 * 文件: Win32Monitor.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   声明监视器信息结构 MonitorInfo 与枚举接口 EnumerateMonitors。
 *   不涉及复杂算法；纯数据结构与接口声明。
 *   不涉及空间-时间权衡。
 *
 * 设计哲学:
 *   以 POD 结构携带监视器句柄与边界，跨层传递显示器信息。
 *
 * 技术特性:
 *   - 监视器结构：HMONITOR 句柄 + RECT 边界矩形
 *   - 枚举接口：expected 包装返回向量，错误时给出描述
 *
 * 依赖关系:
 *   外部依赖：Win32 API（HMONITOR、RECT）、C++ 标准库 <expected>/<vector>
 *
 * 注意事项:
 *   MonitorInfo 为聚合初始化友好的 POD 结构
 *
 ******************************************************************************/
#pragma once
#include <Windows.h>
#include <expected>
#include <string>
#include <vector>
// Win32 监视器管理命名空间
namespace ih::platform {
// 监视器信息结构：句柄 + 屏幕边界矩形（物理像素）
struct MonitorInfo { HMONITOR handle{}; RECT bounds{}; };
// 枚举所有显示监视器
std::expected<std::vector<MonitorInfo>, std::string> EnumerateMonitors();
}
