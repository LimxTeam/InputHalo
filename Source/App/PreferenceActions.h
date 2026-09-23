/*******************************************************************************
 * 文件: PreferenceActions.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   声明 HandlePreferenceAction 函数，负责处理设置面板中配置预设库的用户动作。
 *   算法复杂度：不涉及复杂算法。
 *   空间-时间权衡：不涉及。
 *
 * 设计哲学:
 *   将预设库操作的业务逻辑从 Application 主循环中解耦为独立函数，
 *   保持主循环简洁。函数签名通过引用传递面板与设置模型，直接修改状态。
 *
 * 技术特性:
 *   - 单一入口函数: 所有预设库动作通过同一函数分发。
 *
 * 依赖关系:
 *   内部: ih::ui::LibraryPanel、ih::SettingsModel
 *   外部: Win32 HWND 类型、std::filesystem::path
 *
 * 注意事项:
 *   - Windows 平台依赖 HWND。
 *
 ******************************************************************************/
// 头文件保护
#pragma once
// 库面板 UI：预设列表与动作枚举
#include "UI/LibraryPanel.h"
// Windows 头文件：HWND 类型
#include <Windows.h>
namespace ih {
// HandlePreferenceAction — 处理预设库面板动作的函数声明
// 参数: owner — 所有者窗口句柄; directory — 预设库目录; panel — 库面板引用
//       action — 用户动作枚举; settings — 设置模型引用
// 返回: true 表示设置已变更
bool HandlePreferenceAction(HWND owner,const std::filesystem::path& directory,ui::LibraryPanel& panel,ui::LibraryAction action,SettingsModel& settings);
}
