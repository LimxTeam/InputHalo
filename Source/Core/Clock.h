/*******************************************************************************
 * 文件: Clock.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   高精度时钟工具头文件，提供全局 Now() 内联函数返回 steady_clock 时间戳（秒，double）。
 *   用于动画时间计算、帧调度、事件时间戳等。
 *   算法复杂度：O(1)，单次系统时钟读取。
 *   空间-时间权衡：无额外内存开销，inline 函数零调用成本。
 *
 * 设计哲学:
 *   使用 std::chrono::steady_clock 而非 system_clock，确保时间单调递增，
 *   不受系统时间调整（如 NTP 校时）影响，适合动画计时与帧调度。
 *   头文件内联实现，无需单独编译单元。
 *
 * 技术特性:
 *   - 单调时钟: steady_clock 保证时间不会回退。
 *   - 秒级 double: 直接以秒为单位返回 double，简化动画时间计算。
 *
 * 依赖关系:
 *   外部: <chrono> 标准库
 *
 * 注意事项:
 *   - 时间起点为程序启动后首次调用 steady_clock 的时刻，不代表墙钟时间。
 *
 ******************************************************************************/
// 头文件保护
#pragma once
// C++ 时间库：steady_clock
#include <chrono>
// ih 命名空间
namespace ih {
// Now — 返回当前高精度时间戳（秒，double），基于 steady_clock，单调递增
inline double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
}
