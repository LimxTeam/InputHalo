/*******************************************************************************
 * 文件: Events.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   定义 UI 层事件类型枚举与事件结构体。UiEventType 枚举描述窗口/面板交互事件的种类，
 *   UiEvent 结构体携带事件类型、坐标、滚轮量、按键与修饰键状态。
 *   算法复杂度：不涉及复杂算法。
 *   空间-时间权衡：不涉及。
 *
 * 设计哲学:
 *   使用简单聚合结构体而非继承体系，减少虚表开销，适合高频事件分发。
 *   事件由窗口层产生，经坐标转换后传递给设置面板与覆盖场景处理。
 *
 * 技术特性:
 *   - 值语义事件: 纯数据结构体，无方法，易于拷贝与队列存储。
 *
 * 依赖关系:
 *   内部: ih::input::Key (按键枚举)
 *   外部: 无
 *
 * 注意事项:
 *   - 坐标单位为物理像素，使用前需根据 DPI 缩放转换。
 *
 ******************************************************************************/
// 头文件保护
#pragma once
// 按键枚举定义
#include "Input/Keys.h"
// ih 命名空间
namespace ih {
// UiEventType 枚举：UI 事件类型
// Move=鼠标移动, Down=按键按下, Up=按键释放, Wheel=滚轮,
// KeyDown=键盘按下, KeyUp=键盘释放, Blur=窗口失焦
enum class UiEventType { Move, Down, Up, Wheel, KeyDown, KeyUp, Blur };
// UiEvent 结构体：单次 UI 事件的数据载体
// type=事件类型, x/y=坐标, wheel=滚轮增量, key=按键, shift=Shift 修饰键状态
struct UiEvent { UiEventType type{}; float x{}, y{}, wheel{}; input::Key key{}; bool shift{}; };
}
