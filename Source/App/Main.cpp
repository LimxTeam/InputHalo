/*******************************************************************************
 * 文件: Main.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   Windows 图形子系统程序入口点（wWinMain）。负责设置应用用户模型 ID
 *   （AppUserModelID，使任务栏图标正确分组）、初始化 DPI 感知、创建 Application
 *   实例并启动主循环。同时提供全局异常捕获，将未处理异常以消息框形式展示给用户。
 *   算法复杂度：不涉及复杂算法。
 *   空间-时间权衡：不涉及。
 *
 * 设计哲学:
 *   入口层保持极薄：仅做平台初始化与异常兜底，所有业务逻辑委托给 Application 类。
 *   使用 Windows 图形子系统入口（wWinMain 而非 main），确保无控制台窗口弹出。
 *
 * 技术特性:
 *   - AppUserModelID: 设置唯一的应用用户模型 ID，使 Windows 任务栏正确分组图标。
 *   - DPI 感知初始化: 在创建任何窗口前调用 DPI 感知 API。
 *   - 全局异常捕获: try-catch 包裹整个启动流程，用户可见的错误消息框。
 *
 * 依赖关系:
 *   内部: ih::Application、ih::platform::InitializeDpi
 *   外部: Win32 API (SetCurrentProcessExplicitAppUserModelID, MessageBox)、
 *         ShObjIdl.h (Shell API)
 *
 * 注意事项:
 *   - Windows 平台专用，使用 wWinMain 入口。
 *   - 未处理异常通过 MessageBoxA 展示，同时输出到调试器。
 *
 ******************************************************************************/
// Application 类头文件
#include "App/Application.h"
// DPI 初始化工具：设置进程 DPI 感知级别
#include "Platform/Windows/Win32Dpi.h"
// 标准异常类：用于全局 catch
#include <exception>
// Windows Shell API：SetCurrentProcessExplicitAppUserModelID
#include <ShObjIdl.h>

// ========================================================================
// wWinMain — Windows 图形子系统程序入口
// 参数: instance — 当前实例句柄; showCommand — 窗口显示方式
// 返回: 进程退出码
// ========================================================================
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
// 使用 try-catch 包裹整个启动流程，捕获所有未处理异常
    try {
// 设置应用用户模型 ID，使任务栏图标正确分组（不合并到其他进程）
        SetCurrentProcessExplicitAppUserModelID(L"LimxTeam.InputHalo");
// 初始化进程 DPI 感知；失败时输出调试信息并退出
        if (auto dpi = ih::platform::InitializeDpi(); !dpi) {
// 创建 Application 实例并运行主循环
            OutputDebugStringA(dpi.error().c_str());
// 捕获标准异常：输出调试信息并弹出错误消息框
            return 1;
        }
        ih::Application application;
        return application.Run(instance, showCommand);
    } catch (const std::exception& error) {
        OutputDebugStringA(error.what());
        MessageBoxA(nullptr,error.what(),"InputHalo could not continue",MB_OK|MB_ICONERROR);
        return 1;
    }
}
