/*******************************************************************************
 * 文件: Win32Input.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   声明 Raw Input 采集类 Win32Input 与虚拟键映射函数 MapKey。
 *   不涉及复杂算法；接口声明。
 *   空间-时间权衡：事件向量按需交换（TakeEvents），取走后清空。
 *
 * 设计哲学:
 *   隐藏窗口类封装全局输入采集，对外暴露事件队列与静态工具方法。
 *
 * 技术特性:
 *   - 映射函数：MapKey 将 Win32 键码转为逻辑 Key
 *   - 采集类：管理隐藏窗口、Raw Input 注册、事件队列
 *   - 事件交换：TakeEvents 原子取出并清空队列
 *   - 静态工具：光标位置、桌面可用性、减少动效查询
 *
 * 依赖关系:
 *   内部依赖：ih::input::Event/Key、ih::render2d::Point
 *   外部依赖：Win32 API（Windows.h）、C++ 标准库
 *
 * 注意事项:
 *   Win32Input 实例绑定到创建的隐藏窗口，不可拷贝
 *
 ******************************************************************************/
#pragma once
#include <Windows.h>
#include "Input/InputState.h"
#include <expected>
#include <string>
#include <vector>
#include <utility>
#include <optional>
// Win32 输入采集命名空间
namespace ih::platform {
// 虚拟键映射函数声明
input::Key MapKey(uint16_t virtualKey, uint16_t scan, uint16_t flags);
// Raw Input 采集类：后台全局键盘/鼠标输入
// 通过隐藏窗口接收 WM_INPUT，输出 input::Event 队列
class Win32Input {
public:
    ~Win32Input();
    // 初始化采集窗口与 Raw Input 注册
    std::expected<void, std::string> Initialize();
    // 原子取出事件队列（取走后清空）
    std::vector<input::Event> TakeEvents() { return std::exchange(m_events, {}); }
    // 是否发生不可恢复错误
    bool Failed() const { return m_failed; }
    // 静态：获取光标坐标
    static render2d::Point CursorPosition();
    // 静态：尝试获取光标坐标
    static std::optional<render2d::Point> TryCursorPosition();
    // 静态：当前桌面是否可用（非锁屏/安全桌面）
    static bool DesktopAvailable();
    // 静态：系统是否要求减少动效
    static bool ReduceMotion();
private:
    // 静态窗口过程回调
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    // 读取 Raw Input 并填充事件队列
    void Read(HRAWINPUT handle);
    // 隐藏接收窗口句柄
    HWND m_window{};
    // 待消费的输入事件队列
    std::vector<input::Event> m_events;
    // 错误标志
    bool m_failed{};
};
}
