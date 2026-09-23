#pragma once
#include "Input/InputState.h"
#include "Render2D/Painter.h"
#include "Settings/SettingsModel.h"
#include "Text/TextEngine.h"
#include <deque>
namespace ih::features {
struct Ripple { render2d::Point position; render2d::Color color; double born{}; };
struct LaserPoint { render2d::Point position; double born{}; uint64_t stroke{}; };
render2d::Point Anchor(std::string_view anchor, float width, float height, float itemWidth, float itemHeight, float offsetX, float offsetY);
class OverlayScene {
public:
    explicit OverlayScene(text::TextEngine& text) : m_text(text) {}
    void Apply(const input::Event& event, const SettingsModel& settings);
    void Update(double now, render2d::Point cursor, const SettingsModel& settings);
    void Reset();
    bool Animating(double now, const SettingsModel& settings) const;
    render2d::DrawList Paint(render2d::Rect desktopBounds, float dpi, bool inputMonitor, double now, const SettingsModel& settings);
    const input::State& Input() const { return m_input; }
    size_t RippleCount() const { return m_ripples.size(); }
    size_t LaserCount() const { return m_laser.size(); }
    bool DrawingActive(const SettingsModel& settings)const;
    static void PaintCursor(render2d::Painter& painter, render2d::Point center, const SettingsModel& settings);
    static void PaintAnnotation(render2d::Painter& painter,render2d::Point start,render2d::Point end,std::string_view shape,float width,render2d::Color color);
private:
    text::TextEngine& m_text;
    input::State m_input;
    render2d::Point m_cursor;
    std::deque<Ripple> m_ripples;
    std::deque<LaserPoint> m_laser;
    bool m_laserHeld{};
    uint64_t m_stroke{};
    struct Box {render2d::Point start,end;std::string shape;};
    std::deque<Box> m_boxes;
    bool m_drawing=false;
};
}
