#pragma once
#include "Core/Events.h"
#include "Text/TextEngine.h"
#include "Render2D/Painter.h"
#include <string>
#include <vector>
namespace ih::ui {
class AboutPanel {
public:
 explicit AboutPanel(text::TextEngine& text):m_text(text){}
 void Event(const UiEvent& event);
 void Paint(render2d::Painter& painter,float width,float height,float dpi,bool chinese);
private:
 void Layout(float width,float dpi,bool chinese);
 text::TextEngine& m_text;
 std::vector<std::string> m_lines;
 int m_tab=0,m_cachedTab=-1;
 float m_scroll=0,m_width=0,m_dpi=0,m_viewHeight=0;
 bool m_chinese=false;
};
}
