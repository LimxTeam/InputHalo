/*******************************************************************************
 * 文件: Win32Fonts.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   声明系统默认字体路径查询接口。
 *   不涉及复杂算法；纯接口声明。
 *   不涉及空间-时间权衡。
 *
 * 设计哲学:
 *   暴露 DefaultFontPaths 供渲染层初始化字体集合。
 *
 * 技术特性:
 *   - 接口声明：返回字体文件路径向量
 *
 * 依赖关系:
 *   外部依赖：C++ 标准库 <filesystem>、<vector>
 *
 * 注意事项:
 *   系统字体就地引用，不分发
 *
 ******************************************************************************/
#pragma once
#include <filesystem>
#include <vector>
// Win32 字体管理命名空间
namespace ih::platform {
// Installed system fonts are used in place, never redistributed with the application.
// 获取系统默认字体路径列表（就地引用已安装字体）
std::vector<std::filesystem::path> DefaultFontPaths();
}
