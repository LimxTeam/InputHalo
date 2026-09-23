/*******************************************************************************
 * 文件: Win32Dpi.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   声明 DPI 初始化接口。
 *   不涉及复杂算法；纯接口声明。
 *   不涉及空间-时间权衡。
 *
 * 设计哲学:
 *   以最小头文件暴露 DPI 初始化函数，供 main 在窗口创建前调用。
 *
 * 技术特性:
 *   - 接口声明：InitializeDpi 返回 expected 以报告错误
 *
 * 依赖关系:
 *   外部依赖：C++ 标准库 <expected>、<string>
 *
 * 注意事项:
 *   必须在任何窗口创建前调用
 *
 ******************************************************************************/
#pragma once
#include <expected>
#include <string>
// Win32 DPI 适配命名空间
namespace ih::platform {
// 初始化 Per-Monitor V2 DPI 感知，成功返回 void
std::expected<void, std::string> InitializeDpi();
}
