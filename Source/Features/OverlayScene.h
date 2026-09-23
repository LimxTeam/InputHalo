/*******************************************************************************
 * 文件: OverlayScene.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   覆盖层场景头文件声明。OverlayScene 是输入可视化的核心场景类，负责：
 *   接收原始输入事件（键鼠）、管理光标指示、点击涟漪特效、激光笔轨迹、
 *   屏幕绘图标注（矩形/椭圆/箭头）、以及按键/鼠标状态的屏幕叠加显示。
 *   包含 Ripple（涟漪）、LaserPoint（激光轨迹点）、Box（绘图框）等数据结构。
 *   算法复杂度：绘制 O(N)，N 为特效元素数量；输入处理 O(1)。
 *   空间-时间权衡：使用 deque 存储特效元素，尾部插入头部弹出，
 *   限制最大元素数量（涟漪 32、激光 512、框 64）防止内存膨胀。
 *
 * 设计哲学:
 *   场景类仅维护状态与绘制逻辑，不直接操作窗口或 RHI。
 *   Paint() 方法返回 DrawList 命令列表，由 OverlayManager 提交到 Vulkan 渲染。
 *   输入事件通过 Apply() 送入，动画进度通过 Update() 推进，
 *   是否需要持续动画由 Animating() 查询，主循环据此决定是否重绘。
 *
 * 技术特性:
 *   - 多特效叠加: 光标指示 + 点击涟漪 + 激光笔 + 屏幕绘图 + 按键显示。
 *   - 状态机: DrawingActive 判断绘图模式是否激活（热键按下 + 绘图启用）。
 *   - 帧动画: Animating() 检查是否有正在播放的动画，驱动按需渲染。
 *
 * 依赖关系:
 *   内部: ih::input::State、ih::render2d::Painter/DrawList、ih::SettingsModel、
 *         ih::text::TextEngine
 *   外部: <deque>
 *
 * 注意事项:
 *   - 场景不持有窗口句柄，所有坐标为桌面绝对坐标。
 *   - Paint() 是 const 方法，不修改场景状态。
 *
 ******************************************************************************/
// 头文件保护
#pragma once
// 输入状态机：跟踪按键/鼠标按下/释放/和弦状态
#include "Input/InputState.h"
// 2D 绘制器：DrawList 命令构建
#include "Render2D/Painter.h"
// 设置数据模型
#include "Settings/SettingsModel.h"
// 文本引擎：文字排版与绘制
#include "Text/TextEngine.h"
// deque 容器：用于存储涟漪/激光轨迹等动态元素
#include <deque>
// ih::features 特性层命名空间
namespace ih::features {
// Ripple 结构体：点击涟漪特效
// position=位置, color=颜色, born=出生时间戳（用于计算动画进度）
struct Ripple { render2d::Point position; render2d::Color color; double born{}; };
// LaserPoint 结构体：激光笔轨迹点
// position=位置, born=出生时间, stroke=笔画编号（区分连续笔画）
struct LaserPoint { render2d::Point position; double born{}; uint64_t stroke{}; };
// Anchor 函数：根据锚点字符串（top-left/center 等）计算元素屏幕位置
render2d::Point Anchor(std::string_view anchor, float width, float height, float itemWidth, float itemHeight, float offsetX, float offsetY);
// OverlayScene 类：输入可视化覆盖层场景核心类
class OverlayScene {
// --- 公共接口 ---
public:
// 构造：注入文本引擎引用（用于按键标签文字渲染）
    explicit OverlayScene(text::TextEngine& text) : m_text(text) {}
// Apply — 送入原始输入事件，更新场景状态
    void Apply(const input::Event& event, const SettingsModel& settings);
// Update — 推进动画状态，清理过期元素
    void Update(double now, render2d::Point cursor, const SettingsModel& settings);
// Reset — 重置场景到初始状态
    void Reset();
// Animating — 查询场景是否正在播放动画（决定是否需要持续重绘）
    bool Animating(double now, const SettingsModel& settings) const;
// Paint — 绘制当前帧，返回绘制命令列表
// 参数: bounds=显示器边界, dpi=DPI 缩放, inputMonitor=是否显示按键监听面板,
//       now=当前时间戳, settings=用户设置
    render2d::DrawList Paint(render2d::Rect desktopBounds, float dpi, bool inputMonitor, double now, const SettingsModel& settings);
// Input — 获取输入状态只读引用
    const input::State& Input() const { return m_input; }
// RippleCount — 当前活跃涟漪数量
    size_t RippleCount() const { return m_ripples.size(); }
// LaserCount — 当前激光轨迹点数量
    size_t LaserCount() const { return m_laser.size(); }
// DrawingActive — 绘图模式是否激活（绘图启用且热键按下）
    bool DrawingActive(const SettingsModel& settings)const;
// PaintCursor — 静态方法：绘制光标指示
    static void PaintCursor(render2d::Painter& painter, render2d::Point center, const SettingsModel& settings);
// PaintAnnotation — 静态方法：绘制屏幕标注（矩形/椭圆/箭头）
    static void PaintAnnotation(render2d::Painter& painter,render2d::Point start,render2d::Point end,std::string_view shape,float width,render2d::Color color);
// --- 私有成员 ---
private:
// m_text — 文本引擎引用（用于按键标签渲染）
    text::TextEngine& m_text;
// m_input — 输入状态机
    input::State m_input;
// m_cursor — 当前光标位置
    render2d::Point m_cursor;
// m_ripples — 点击涟漪队列
    std::deque<Ripple> m_ripples;
// m_laser — 激光笔轨迹点队列
    std::deque<LaserPoint> m_laser;
// m_laserHeld — 激光热键是否按住
    bool m_laserHeld{};
// m_stroke — 当前激光笔画编号（每次按下热键递增）
    uint64_t m_stroke{};
// Box 结构体：屏幕绘图框（起点、终点、形状类型）
    struct Box {render2d::Point start,end;std::string shape;};
// m_boxes — 绘图框队列
    std::deque<Box> m_boxes;
// m_drawing — 是否正在拖曳绘制新框
    bool m_drawing=false;
};
}
