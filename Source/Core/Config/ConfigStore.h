/*******************************************************************************
 * 文件: ConfigStore.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   配置存储的头文件声明，定义 ConfigLoadResult 结构体与 LoadConfig/SaveConfig 函数。
 *   算法复杂度：不涉及复杂算法。
 *   空间-时间权衡：不涉及。
 *
 * 设计哲学:
 *   使用 std::expected 返回错误而非异常，让调用方显式处理错误路径。
 *   ConfigLoadResult 同时携带设置与警告信息（如配置损坏但已回退默认），
 *   使调用方能区分"加载失败"与"加载成功但有警告"。
 *
 * 技术特性:
 *   - std::expected 返回值: 类型安全的错误处理。
 *
 * 依赖关系:
 *   内部: ih::SettingsModel
 *   外部: <expected>, <filesystem>, <string>
 *
 * 注意事项:
 *   - 配置文件格式为 JSON。
 *
 ******************************************************************************/
// 头文件保护
#pragma once
// 设置数据模型
#include "Settings/SettingsModel.h"
// std::expected：类型安全的错误返回
#include <expected>
// 文件系统路径
#include <filesystem>
// 字符串
#include <string>

// ih 命名空间
namespace ih {
// ConfigLoadResult 结构体：配置加载结果
// settings — 加载的设置模型; warning — 警告信息（如配置已回退默认）
struct ConfigLoadResult {
    SettingsModel settings;
    std::string warning;
};
// LoadConfig — 加载配置文件，返回结果或错误
std::expected<ConfigLoadResult, std::string> LoadConfig(const std::filesystem::path& path);
// SaveConfig — 原子保存配置文件，成功返回 void，失败返回错误
std::expected<void, std::string> SaveConfig(const std::filesystem::path& path, const SettingsModel& settings);
}
