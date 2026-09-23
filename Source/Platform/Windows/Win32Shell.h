/*******************************************************************************
 * 文件: Win32Shell.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   声明开机自启接口与系统托盘类 Win32Tray。
 *   不涉及复杂算法；接口声明。
 *   不涉及空间-时间权衡。
 *
 * 设计哲学:
 *   托盘类封装 Shell 通知图标，菜单动作以整数编码返回，由主循环解释执行。
 *
 * 技术特性:
 *   - 开机自启：SetRunAtStartup 控制注册表 Run 键
 *   - 托盘类：初始化、动作消费、启用/中文状态切换
 *
 * 依赖关系:
 *   外部依赖：Win32 API（shellapi.h）、C++ 标准库
 *
 * 注意事项:
 *   TakeAction 返回 0 表示无动作；1=打开 2=切换覆盖 3=退出
 *
 ******************************************************************************/
#pragma once
#include <Windows.h>
#include <shellapi.h>
#include <expected>
#include <string>
#include <utility>
// Win32 Shell 集成命名空间
namespace ih::platform {
// 设置开机自启（注册表 Run 键）
std::expected<void,std::string> SetRunAtStartup(bool enabled);
// 系统托盘图标类
// 管理托盘图标与右键菜单，动作通过 TakeAction 消费
class Win32Tray {
public:
 ~Win32Tray();
    // 初始化托盘（传入设置窗口句柄）
 bool Initialize(HWND settings);
    // 动作编码注释：1=打开 2=切换覆盖 3=退出（仅菜单动作，无输入数据）
 // 1=open, 2=toggle overlays, 3=exit. Menu actions only; no input data.
    // 原子消费托盘动作（0=无动作）
 int TakeAction(){return std::exchange(m_action,0);}
    // 设置覆盖启用状态（影响菜单项文案）
 void Enabled(bool value){m_enabled=value;}
    // 设置中文界面（影响菜单文案）
 void Chinese(bool value){m_chinese=value;}
private:
    // 静态窗口过程
 static LRESULT CALLBACK Proc(HWND,UINT,WPARAM,LPARAM);
    // 重新添加托盘图标
 void Add();
    // 托盘窗口、设置窗口、图标数据、动作、启用状态、任务栏重启消息
 HWND m_window{},m_settings{};NOTIFYICONDATAW m_icon{};int m_action{};bool m_enabled=true;UINT m_restart{};
    // 中文界面标志
 bool m_chinese=true;
};
}
