#pragma once
#include "Render2D/DrawList.h"

namespace ih::render2d {
// No platform or GPU dependencies. Painter order is preserved across clip batches.
class Painter {
public:
    Painter(float width, float height);
    void FillRect(Rect bounds, Color color);
    void RoundedRect(Rect bounds, float radius, Color color);
    void Circle(Point center, float radius, Color color);
    void Ring(Point center, float radius, float width, Color color);
    void Glow(Point center, float radius, Color color);
    void Line(Point start, Point end, float width, Color color);
    void Text(const text::Layout& layout, Point origin, Color color);
    void PushClip(Rect clip);
    void PopClip();
    const DrawList& List() const { return m_list; }
private:
    void Add(Command command);
    DrawList m_list;
    std::vector<Rect> m_clips;
};
}
