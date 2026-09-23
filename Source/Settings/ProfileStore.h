/*******************************************************************************
 * 文件: ProfileStore.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   声明配置方案(Profile)的数据结构及其读取、导入、应用的纯接口。
 *   方案是一组可跨机器导入导出的“可视化预设”；运行时设置模型由 SettingsModel 承载。
 *   复杂度: 目录枚举为 O(文件数)，导入校验为 O(方案数 × 字段数)。
 *
 * 设计哲学:
 *   方案只描述外观与标注等可共享状态，刻意不含本机状态(开机启动/语言/排除规则)，
 *   因此导入外部方案不会污染当前机器配置；接口用 std::expected 返回错误而非抛异常。
 *
 * 技术特性:
 *   - Profile 用 builtin 标志区分内置预设与用户 JSON 文件
 *   - ReadProfiles 扫描目录并合并内置方案
 *   - ApplyProfile 做分区级别的受控合并
 *
 * 依赖关系:
 *   内部: Settings/SettingsModel.h
 *   外部: <filesystem>、<expected>、<vector>，nlohmann/json(经实现层)
 *
 * 注意事项:
 *   仅 Windows x64；方案文件大小上限 256 KiB；头文件不引入 json 依赖。
 ******************************************************************************/
#pragma once
#include "Settings/SettingsModel.h"
#include <filesystem>
#include <expected>
#include <vector>
// InputHalo 配置层命名空间：配置存储与设置模型位于 ih 之下。
namespace ih {
// 单个配置方案：名称、来源文件路径(内置为空)、实际设置内容、是否内置预设。
struct Profile {std::string name;std::filesystem::path path;SettingsModel settings;bool builtin{};};
// 扫描目录返回内置 + 用户方案列表；目录不存在时会被创建。
std::vector<Profile> ReadProfiles(const std::filesystem::path& directory);
// 从外部 JSON 文件导入并校验方案，失败时返回字符串错误原因。
std::expected<SettingsModel,std::string> ImportProfile(const std::filesystem::path& path);
// 以默认值为基线，用给定方案覆盖出一份独立的新设置。
SettingsModel ProfileSettings(const SettingsModel& source);
// 把方案中的可见性分区合并进目标设置(不改目标的本机状态)。
void ApplyProfile(SettingsModel& target,const SettingsModel& profile);
}
