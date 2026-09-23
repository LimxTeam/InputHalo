/*******************************************************************************
 * 文件: InputState.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   声明输入状态机 State 及其相关事件 Event、和弦 Chord、事件类型 EventType。
 *   不涉及复杂算法；纯数据结构与接口声明。
 *   空间-时间权衡：m_down 为定长 bool 数组（KeyCount 项），查询 O(1)；m_chords 用 deque 保留最近记录。
 *
 * 设计哲学:
 *   以纯头文件声明状态接口，使渲染层与平台层通过一致的数据结构交互；事件携带光标位置与时间戳。
 *
 * 技术特性:
 *   - 事件枚举：区分按下、松开、滚轮、重置四种事件类型
 *   - 事件结构：携带按键、滚轮增量、时间戳、光标坐标
 *   - 和弦结构：记录修饰键组合、释放/触摸时间、重复次数
 *   - 状态类：聚合按键矩阵与和弦队列，提供热键匹配与过期管理
 *
 * 依赖关系:
 *   内部依赖：ih::input::Key（Keys.h）、render2d::Point（Render2D/DrawList.h）
 *   外部依赖：C++ 标准库 <array>、<deque>
 *
 * 注意事项:
 *   State 类不持有线程锁，假设单线程消息循环消费事件
 *   mouseReleased/wheelTime 初始化为 -100，保证首次渲染时视为过期
 *
 ******************************************************************************/
#pragma once
#include "Input/Keys.h"
#include "Render2D/DrawList.h"
#include <array>
#include <deque>
// 输入状态命名空间
namespace ih::input {
// 事件类型枚举：原始输入事件的四种语义
enum class EventType { Down, Up, Wheel, Reset };
// 输入事件结构：一条由平台层上报的原始输入
// 携带类型、按键、滚轮增量、时间戳与光标位置，供 State::Apply 消费
struct Event {
    EventType type{};
    Key key{};
    float wheel{};
    double time{};
    render2d::Point position{};
};
// 和弦（Chord）结构：一次修饰键+主键的组合按键记录
// 用于渲染按键光环：released 为 -1 表示仍按住，touched 为最近触摸时间
struct Chord {
    Hotkey combination;
    double released = -1;
    double touched{};
    unsigned repeats = 1;
};
// 输入状态机：聚合全局按键矩阵与和弦队列
// 是 Input 层的核心状态容器，被平台层写入、被渲染层读取
class State {
public:
    void Apply(const Event& event, bool shortcutOnly);
    void Expire(double now, double lifetime);
    void Reset();
    // O(1) 查询某键是否按下
    bool Down(Key key) const { return m_down[Index(key)]; }
    // 获取当前修饰键状态（左右合并），可附带主键
    Hotkey Modifiers(Key primary = Key::Unknown) const;
    // 判断热键是否被按住
    bool Held(const Hotkey& key) const;
    // 只读访问和弦队列，供渲染层绘制按键轨迹
    const std::deque<Chord>& Chords() const { return m_chords; }
    bool AnyMouse() const;
    // 鼠标最近释放时间戳（秒），用于鼠标点击光环
    double mouseReleased = -100;
    // 滚轮事件时间戳（秒）
    double wheelTime = -100;
    // 滚轮方向增量
    float wheelDirection{};
private:
    // 按键按下矩阵：下标即 Key 枚举值，O(1) 查询
    std::array<bool, KeyCount> m_down{};
    // 最近和弦队列：保留最近若干组按键组合
    std::deque<Chord> m_chords;
};
}
