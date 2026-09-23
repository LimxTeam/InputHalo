/*******************************************************************************
 * 文件: Painter.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   2D 画笔实现：维护裁剪栈、校验几何与颜色、把各种图元（矩形/圆角/圆/环/辉光/线/文字）
 *   翻译为统一的 64 字节 Command 流，并按裁剪区间聚合为 Batch。
 *   不涉及复杂算法；批次合并为线性扫描 O(n)，裁剪求交为常数时间。
 *   以少量冗余校验（isfinite/clamp）换取绘制数据的合法性与 GPU 端稳定性。
 *
 * 设计哲学:
 *   Painter 是纯 CPU、无平台/GPU 依赖的录制器：调用顺序即绘制顺序，
 *   裁剪通过栈式 PushClip/PopClip 表达，新裁剪与父裁剪求交后生效。
 *   所有图元最终归一为 FillRect 风格的包围盒 + 形状参数，RHI 无需了解具体图元语义。
 *
 * 技术特性:
 *   - 裁剪栈: PushClip 与父裁剪求交，PopClip 恢复，根裁剪不允许弹出。
 *   - 输入防御: 对非有限值、负尺寸、越界命令直接抛异常，尽早暴露调用错误。
 *   - 颜色归一: 写入前统一 clamp 到 [0,1]，透明/零面积图元直接剔除。
 *   - 批次合并: 相邻命令若裁剪矩形相同则并入当前 Batch，否则新开 Batch。
 *
 * 依赖关系:
 *   内部: Limx::ih::render2d（DrawList/Command/Batch）、Limx::text::Layout/Atlas
 *   外部: C++23 <algorithm>/<cmath>/<stdexcept>
 *
 * 注意事项:
 *   单 DrawList 只能绑定一个文字引擎（Atlas identity 必须一致）。
 *   命令数超过 MaxCommands 会抛 std::length_error。
 *   Painter 不负责提交，仅生成 DrawList，由 RHI 层消费。
 *
 ******************************************************************************/
#include "Render2D/Painter.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ih::render2d {
namespace {
// (匿名命名空间) 校验矩形合法性：要求有限值且宽高非负，否则抛 invalid_argument。
void Valid(Rect r) {
    if (!std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.width) || !std::isfinite(r.height)
        || !std::isfinite(r.x + r.width) || !std::isfinite(r.y + r.height) || r.width < 0 || r.height < 0)
        throw std::invalid_argument("Invalid 2D rectangle");
}
// 判断两个矩形是否逐字段相等，用于决定能否并入同一批次。
bool Same(Rect a, Rect b) { return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height; }
}
// 构造：以整个画布为根裁剪；非法尺寸在入口即报错。
Painter::Painter(float width, float height) { Valid({0, 0, width, height}); m_clips.push_back({0, 0, width, height}); }
// PushClip: 新裁剪与栈顶父裁剪求交后压栈，形成嵌套裁剪。
void Painter::PushClip(Rect r) {
    Valid(r);
    const auto parent = m_clips.back();
    const float x = std::max(r.x, parent.x), y = std::max(r.y, parent.y);
    m_clips.push_back({x, y, std::max(0.f, std::min(r.x + r.width, parent.x + parent.width) - x),
        std::max(0.f, std::min(r.y + r.height, parent.y + parent.height) - y)});
}
// PopClip: 弹出一层裁剪；不允许弹出根裁剪（否则逻辑错误）。
void Painter::PopClip() {
    if (m_clips.size() == 1) throw std::logic_error("Cannot pop root clip");
    m_clips.pop_back();
}
// Add: 命令写入主入口——校验、归一颜色、剔除空图元、追加命令并维护批次。
void Painter::Add(Command command) {
    Valid(command.bounds);
    auto& color = command.color;
    if (!std::isfinite(color.r) || !std::isfinite(color.g) || !std::isfinite(color.b) || !std::isfinite(color.a))
        throw std::invalid_argument("Nonfinite 2D color");
    color = {std::clamp(color.r, 0.f, 1.f), std::clamp(color.g, 0.f, 1.f), std::clamp(color.b, 0.f, 1.f), std::clamp(color.a, 0.f, 1.f)};
// 当前生效裁剪；零面积裁剪/零面积图元/全透明直接丢弃，不污染命令流。
    const auto clip = m_clips.back();
    if (!clip.width || !clip.height || !command.bounds.width || !command.bounds.height || !color.a) return;
    if (m_list.commands.size() >= MaxCommands) throw std::length_error("2D command capacity exceeded");
    const auto index = static_cast<uint32_t>(m_list.commands.size());
    m_list.commands.push_back(command);
// 若与上一批次裁剪相同则计数+1（合并），否则新开一个批次。
    if (!m_list.batches.empty() && Same(m_list.batches.back().clip, clip)) ++m_list.batches.back().count;
    else m_list.batches.push_back({index, 1, clip});
}
// FillRect: 实心矩形，最基础的图元。
void Painter::FillRect(Rect bounds, Color color) { Add({bounds, color}); }
// Text: 把文字布局中的每个字形翻译为一个 Glyph 命令，UV 写入 endpoints。
void Painter::Text(const text::Layout& layout, Point origin, Color color) {
    if (!layout.atlas) throw std::invalid_argument("Text layout has no atlas");
// 一个 DrawList 只能使用同一文字引擎；图集取最新 revision 以便上传。
    if (m_list.atlas && m_list.atlas->identity != layout.atlas->identity)
        throw std::invalid_argument("One DrawList must use one text engine");
    if (!m_list.atlas || m_list.atlas->revision < layout.atlas->revision) m_list.atlas = layout.atlas;
    for (const auto& glyph : layout.glyphs)
        Add({{origin.x + glyph.x, origin.y + glyph.y, glyph.width, glyph.height}, color,
            {4, 0, static_cast<float>(glyph.page)}, {glyph.u, glyph.v, glyph.uEnd, glyph.vEnd}});
}
// RoundedRect: 圆角矩形，半径钳制到不超过短边一半。
void Painter::RoundedRect(Rect bounds, float radius, Color color) {
    Valid(bounds);
    if (!std::isfinite(radius) || radius < 0) throw std::invalid_argument("Invalid corner radius");
    Add({bounds, color, {1, std::min(radius, std::min(bounds.width, bounds.height) * .5f)}});
}
// Circle: 实心圆，用包围盒 + parameters[1]=半径表达。
void Painter::Circle(Point center, float radius, Color color) {
    if (!std::isfinite(radius) || radius < 0) throw std::invalid_argument("Invalid circle radius");
    Add({{center.x - radius, center.y - radius, radius * 2, radius * 2}, color, {2, radius}});
}
// Ring: 圆环（扇形描边），内半径由半径减描边宽得到；零宽度直接跳过。
void Painter::Ring(Point center, float radius, float width, Color color) {
    if (!std::isfinite(radius) || !std::isfinite(width) || radius < 0 || width < 0) throw std::invalid_argument("Invalid ring");
    if (!width) return;
    Add({{center.x - radius, center.y - radius, radius * 2, radius * 2}, color, {5, radius, std::min(width, radius * 2)}});
}
// Glow: 辉光/光晕图元，半径控制衰减范围。
void Painter::Glow(Point center, float radius, Color color) {
    if (!std::isfinite(radius) || radius < 0) throw std::invalid_argument("Invalid glow");
    Add({{center.x - radius, center.y - radius, radius * 2, radius * 2}, color, {6, radius}});
}
// Line: 线段，扩展出半宽包围盒，端点写入 endpoints 供着色器插值。
void Painter::Line(Point start, Point end, float width, Color color) {
    if (!std::isfinite(start.x) || !std::isfinite(start.y) || !std::isfinite(end.x) || !std::isfinite(end.y)
        || !std::isfinite(width) || width < 0) throw std::invalid_argument("Invalid line geometry");
    if (width == 0) return;
    const auto half = width * .5f;
    Add({{std::min(start.x, end.x) - half, std::min(start.y, end.y) - half,
        std::abs(end.x - start.x) + width, std::abs(end.y - start.y) + width}, color,
        {3, half}, {start.x, start.y, end.x, end.y}});
}
}
