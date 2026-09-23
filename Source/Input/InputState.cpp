/*******************************************************************************
 * 文件: InputState.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   实现输入状态机：跟踪按键/鼠标按下状态、修饰键组合（Chord）、滚轮事件与手势过期逻辑。
 *   算法复杂度：按键查询 O(1)（数组下标），和弦管理 O(1) 摊还（deque 尾部追加/弹出），过期清理 O(n)。
 *   空间-时间权衡：用定长数组 m_down 换取 O(1) 按键查询；用 deque 缓存最近 4 组和弦以展示连击轨迹。
 *
 * 设计哲学:
 *   将原始输入事件流归纳为高层语义（按下/松开/修饰键组合/滚轮），供渲染层消费；状态与平台解耦。
 *
 * 技术特性:
 *   - 热键匹配：同时比对主键与左右修饰键，支持 Ctrl/Shift/Alt/Win 任意组合
 *   - 和弦追踪：记录最近按键组合的触摸时间、释放时间与重复次数，用于绘制按键光环
 *   - 滚轮追踪：独立记录滚轮方向与时间戳，用于绘制滚轮指示器
 *   - 自动过期：按生命周期清理已释放的和弦，避免内存无限增长
 *
 * 依赖关系:
 *   内部依赖：ih::input 命名空间（Keys.h）
 *   外部依赖：C++ 标准库 <algorithm>（std::erase_if）
 *
 * 注意事项:
 *   shortcutOnly 模式下仅记录含修饰键或功能键的组合，过滤纯文本按键
 *   Pause 键会被特殊处理：模拟一次额外的 Up 事件以兼容硬件行为
 *
 ******************************************************************************/
#include "Input/InputState.h"
#include <algorithm>
// 输入状态命名空间：集中管理按键、鼠标与手势状态
namespace ih::input {
// 获取当前主键对应的修饰键状态（左/右合并）
// 将左右 Ctrl/Shift/Alt/Win 合并为逻辑修饰键，便于热键匹配
Hotkey State::Modifiers(Key primary) const {
    return {Down(Key::LeftCtrl) || Down(Key::RightCtrl), Down(Key::LeftShift) || Down(Key::RightShift),
        Down(Key::LeftAlt) || Down(Key::RightAlt), Down(Key::LeftWin) || Down(Key::RightWin), primary};
}
// 判断指定热键当前是否被持续按住
// 要求主键按下且所有声明的修饰键均处于按下状态
bool State::Held(const Hotkey& key) const {
    const auto current = Modifiers(key.primary);
    return key.Valid() && Down(key.primary) && (!key.ctrl || current.ctrl) && (!key.shift || current.shift) &&
        (!key.alt || current.alt) && (!key.win || current.win);
}
// 检查任意鼠标按键是否处于按下状态
bool State::AnyMouse() const {
    for (auto key : {Key::MouseLeft,Key::MouseRight,Key::MouseMiddle,Key::Mouse4,Key::Mouse5}) if (Down(key)) return true;
    return false;
}
// 重置全部输入状态：清空按键、和弦，并将鼠标/滚轮时间戳置为过期
void State::Reset() { m_down.fill(false); m_chords.clear(); mouseReleased = wheelTime = -100; wheelDirection = 0; }
// 将一条原始输入事件应用到状态机
// shortcutOnly=true 时仅记录快捷键组合，忽略普通字符输入
void State::Apply(const Event& event, bool shortcutOnly) {
    // 收到全局重置事件（如设备变更、锁屏、休眠），清空全部状态
    if (event.type == EventType::Reset) { Reset(); return; }
    // 滚轮事件：仅更新方向与时间戳，不影响按键矩阵
    if (event.type == EventType::Wheel) { wheelDirection = event.wheel; wheelTime = event.time; return; }
    // 忽略未知按键或越界索引，防止数组访问异常
    if (event.key == Key::Unknown || Index(event.key) >= KeyCount) return;
    // 记录该键在本次事件前是否已处于按下状态（用于判定自动重复）
    const bool repeated = Down(event.key);
    // 更新按键按下矩阵：根据事件类型置位或清零
    m_down[Index(event.key)] = event.type == EventType::Down;
    // 鼠标按键：仅记录释放时间，不参与和弦追踪
    if (event.key >= Key::MouseLeft) { mouseReleased = event.time; return; }
    // 按键释放：标记所有已按下但当前不再满足组合的和弦为已释放
    if (event.type == EventType::Up) {
        for (auto& chord : m_chords) if (chord.released < 0 && !Held(chord.combination)) chord.released = event.time;
        return;
    }
    // 单独的修饰键按下不构成和弦，直接跳过
    if (IsModifier(event.key)) return;
    // 生成当前修饰键组合（含主键），作为和弦的唯一标识
    const auto combination = Modifiers(event.key);
    // 快捷键过滤：无修饰键且非功能键时不记录（纯文本输入）
    if (shortcutOnly && !combination.ctrl && !combination.shift && !combination.alt && !combination.win && !IsShortcutKey(event.key)) return;
    // 和弦合并：若与上一组组合相同且在 0.25s 内重复触摸，则累计重复次数而非新建
    if (!m_chords.empty() && m_chords.back().combination == combination &&
        (repeated || event.time - m_chords.back().touched <= .25)) {
        auto& chord = m_chords.back(); chord.touched = event.time; chord.released = -1; ++chord.repeats;
    } else {
        // 否则新建一条和弦记录
        m_chords.push_back({combination, -1, event.time, 1});
        // 仅保留最近 4 组和弦，弹出最旧记录以控制内存与渲染复杂度
        while (m_chords.size() > 4) m_chords.pop_front();
    }
}
// 按时间过期清理已释放的和弦
// now 为当前时间，lifetime 为和弦从释放到消失的存活时长
void State::Expire(double now, double lifetime) {
    std::erase_if(m_chords, [&](const Chord& c) { return c.released >= 0 && now - c.released >= lifetime; });
}
}
