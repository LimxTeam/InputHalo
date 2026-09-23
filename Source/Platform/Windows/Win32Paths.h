/*******************************************************************************
 * 文件: Win32Paths.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   声明路径解析接口：可执行目录与用户数据目录。
 *   不涉及复杂算法；纯接口声明。
 *   不涉及空间-时间权衡。
 *
 * 设计哲学:
 *   以 expected 包装路径返回，统一错误处理风格。
 *
 * 技术特性:
 *   - 接口声明：UserDataDirectory、ExecutableDirectory
 *
 * 依赖关系:
 *   外部依赖：C++ 标准库 <expected>、<filesystem>、<string>
 *
 * 注意事项:
 *   路径为宽字符文件系统路径
 *
 ******************************************************************************/
#pragma once
#include <expected>
#include <filesystem>
#include <string>
// Win32 路径管理命名空间
namespace ih::platform {
// 获取用户数据目录（LocalAppData/InputHalo）
std::expected<std::filesystem::path, std::string> UserDataDirectory();
// 获取可执行文件所在目录
std::expected<std::filesystem::path, std::string> ExecutableDirectory();
}
