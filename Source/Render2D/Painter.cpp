#include "Render2D/Painter.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ih::render2d {
namespace {
void Valid(Rect r) {
    if (!std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.width) || !std::isfinite(r.height)
        || !std::isfinite(r.x + r.width) || !std::isfinite(r.y + r.height) || r.width < 0 || r.height < 0)
        throw std::invalid_argument("Invalid 2D rectangle");
}
bool Same(Rect a, Rect b) { return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height; }
}
Painter::Painter(float width, float height) { Valid({0, 0, width, height}); m_clips.push_back({0, 0, width, height}); }
void Painter::PushClip(Rect r) {
    Valid(r);
    const auto parent = m_clips.back();
    const float x = std::max(r.x, parent.x), y = std::max(r.y, parent.y);
    m_clips.push_back({x, y, std::max(0.f, std::min(r.x + r.width, parent.x + parent.width) - x),
        std::max(0.f, std::min(r.y + r.height, parent.y + parent.height) - y)});
}
void Painter::PopClip() {
    if (m_clips.size() == 1) throw std::logic_error("Cannot pop root clip");
    m_clips.pop_back();
}
void Painter::Add(Command command) {
    Valid(command.bounds);
    auto& color = command.color;
    if (!std::isfinite(color.r) || !std::isfinite(color.g) || !std::isfinite(color.b) || !std::isfinite(color.a))
        throw std::invalid_argument("Nonfinite 2D color");
    color = {std::clamp(color.r, 0.f, 1.f), std::clamp(color.g, 0.f, 1.f), std::clamp(color.b, 0.f, 1.f), std::clamp(color.a, 0.f, 1.f)};
    const auto clip = m_clips.back();
    if (!clip.width || !clip.height || !command.bounds.width || !command.bounds.height || !color.a) return;
    if (m_list.commands.size() >= MaxCommands) throw std::length_error("2D command capacity exceeded");
    const auto index = static_cast<uint32_t>(m_list.commands.size());
    m_list.commands.push_back(command);
    if (!m_list.batches.empty() && Same(m_list.batches.back().clip, clip)) ++m_list.batches.back().count;
    else m_list.batches.push_back({index, 1, clip});
}
void Painter::FillRect(Rect bounds, Color color) { Add({bounds, color}); }
void Painter::Text(const text::Layout& layout, Point origin, Color color) {
    if (!layout.atlas) throw std::invalid_argument("Text layout has no atlas");
    if (m_list.atlas && m_list.atlas->identity != layout.atlas->identity)
        throw std::invalid_argument("One DrawList must use one text engine");
    if (!m_list.atlas || m_list.atlas->revision < layout.atlas->revision) m_list.atlas = layout.atlas;
    for (const auto& glyph : layout.glyphs)
        Add({{origin.x + glyph.x, origin.y + glyph.y, glyph.width, glyph.height}, color,
            {4, 0, static_cast<float>(glyph.page)}, {glyph.u, glyph.v, glyph.uEnd, glyph.vEnd}});
}
void Painter::RoundedRect(Rect bounds, float radius, Color color) {
    Valid(bounds);
    if (!std::isfinite(radius) || radius < 0) throw std::invalid_argument("Invalid corner radius");
    Add({bounds, color, {1, std::min(radius, std::min(bounds.width, bounds.height) * .5f)}});
}
void Painter::Circle(Point center, float radius, Color color) {
    if (!std::isfinite(radius) || radius < 0) throw std::invalid_argument("Invalid circle radius");
    Add({{center.x - radius, center.y - radius, radius * 2, radius * 2}, color, {2, radius}});
}
void Painter::Ring(Point center, float radius, float width, Color color) {
    if (!std::isfinite(radius) || !std::isfinite(width) || radius < 0 || width < 0) throw std::invalid_argument("Invalid ring");
    if (!width) return;
    Add({{center.x - radius, center.y - radius, radius * 2, radius * 2}, color, {5, radius, std::min(width, radius * 2)}});
}
void Painter::Glow(Point center, float radius, Color color) {
    if (!std::isfinite(radius) || radius < 0) throw std::invalid_argument("Invalid glow");
    Add({{center.x - radius, center.y - radius, radius * 2, radius * 2}, color, {6, radius}});
}
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
