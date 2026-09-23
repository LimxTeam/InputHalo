/*******************************************************************************
 * 文件: Win32Preferences.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   声明偏好文件选择与前台排除接口。
 *   不涉及复杂算法；纯接口声明。
 *   不涉及空间-时间权衡。
 *
 * 设计哲学:
 *   为设置界面提供文件对话框与排除检测的平台抽象。
 *
 * 技术特性:
 *   - 接口声明：PickPreferenceFile、ForegroundExcluded
 *
 * 依赖关系:
 *   内部依赖：SettingsModel
 *   外部依赖：Win32 API、C++ 标准库
 *
 * 注意事项:
 *   PickPreferenceFile 返回 optional path，区分取消与错误
 *
 ******************************************************************************/
#pragma once
#include <Windows.h>
#include <filesystem>
#include <expected>
#include <optional>
#include "Settings/SettingsModel.h"
// Win32 偏好设置命名空间
namespace ih::platform {
// 弹出文件选择对话框（保存/打开，可选中文标题）
std::expected<std::optional<std::filesystem::path>,std::string> PickPreferenceFile(HWND owner,bool save,bool executable,bool chinese,const std::filesystem::path& initial={});
// 判断前台进程是否被排除
bool ForegroundExcluded(const SettingsModel& settings);
}
