/*******************************************************************************
 * 文件: Application.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   Application 类的头文件声明。Application 是 InputHalo 应用的顶层控制器，
 *   持有主窗口与设置模型，负责协调所有子系统的初始化、主循环与清理。
 *   算法复杂度：不涉及复杂算法。
 *   空间-时间权衡：无特殊权衡。
 *
 * 设计哲学:
 *   采用薄门面（Facade）模式：Application 类本身不实现具体业务逻辑，
 *   而是将职责委托给各子系统模块（OverlayManager、VulkanContext 等），
 *   自身仅负责编排生命周期与主循环调度。
 *
 * 技术特性:
 *   - 单一 Run 入口: 所有子系统的初始化与清理都封装在 Run() 方法内。
 *   - 值语义成员: 窗口与设置模型作为值成员持有，简化生命周期管理。
 *
 * 依赖关系:
 *   内部: ih::platform::Win32Window、ih::SettingsModel
 *   外部: Win32 HINSTANCE 类型
 *
 * 注意事项:
 *   - 仅 Windows 平台可用（依赖 HINSTANCE）。
 *
 ******************************************************************************/
// 头文件保护：防止重复包含
#pragma once
// Win32 窗口封装：主窗口的创建、消息泵、事件处理
#include "Platform/Windows/Win32Window.h"
// 设置数据模型：所有用户偏好的结构化存储与 JSON 序列化
#include "Settings/SettingsModel.h"

// InputHalo 根命名空间
namespace ih {
// Application 类：应用程序顶层控制器，编排所有子系统
class Application {
// --- 公共接口 ---
public:
// Run — 启动应用程序主循环
// 参数: instance — 程序实例句柄; showCommand — 窗口初始显示方式
// 返回: 进程退出码
    int Run(HINSTANCE instance, int showCommand);
// --- 私有成员 ---
private:
// m_settings — 用户设置模型，默认为默认配置值
    SettingsModel m_settings = SettingsModel::Defaults();
// m_window — 主窗口实例（Win32 窗口封装）
    platform::Win32Window m_window;
};
}
