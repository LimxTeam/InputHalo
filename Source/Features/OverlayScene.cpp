#include "Features/OverlayScene.h"
#include "Render2D/Color.h"
#include <algorithm>
#include <cmath>
namespace ih::features {
using namespace render2d;
namespace {
float Smooth(float t) { t = std::clamp(t,0.f,1.f); return t * t * (3 - 2 * t); }
float Fade(double age, double hold, double duration) { return age <= hold ? 1.f : duration <= 0 ? 0.f : 1 - Smooth(static_cast<float>((age-hold)/duration)); }
Point Mix(Point a, Point b, float t) { return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t}; }
}
Point Anchor(std::string_view anchor, float width, float height, float itemWidth, float itemHeight, float x, float y) {
    float px = (width-itemWidth)*.5f;
    if (anchor.ends_with("left")) px = 24;
    if (anchor.ends_with("right")) px = width-itemWidth-24;
    float py = anchor.starts_with("top") ? y : height-itemHeight-y;
    return {std::clamp(px+x,0.f,std::max(0.f,width-itemWidth)),std::clamp(py,0.f,std::max(0.f,height-itemHeight))};
}
void OverlayScene::Reset() { m_input.Reset(); m_ripples.clear(); m_laser.clear(); m_laserHeld=false;m_boxes.clear();m_drawing=false; }
bool OverlayScene::DrawingActive(const SettingsModel& settings)const{
    return settings.values["drawing"]["enabled"].get<bool>()&&m_input.Held(input::ParseHotkey(settings.values["drawing"]["hotkey"].get<std::string>()));
}
void OverlayScene::Apply(const input::Event& event, const SettingsModel& settings) {
    if (event.type == input::EventType::Reset) { Reset(); return; }
    const bool repeated=m_input.Down(event.key);
    m_input.Apply(event, settings.values["input"]["shortcut_only"].get<bool>());
    if(!DrawingActive(settings)){m_boxes.clear();m_drawing=false;}
    else if(event.type==input::EventType::Down&&!repeated&&event.key==input::Key::MouseRight){
        if(!m_boxes.empty())m_boxes.pop_back();m_drawing=false;
    }
    else if(event.key==input::Key::MouseLeft){
        if(event.type==input::EventType::Down&&!m_drawing&&m_boxes.size()<64){m_boxes.push_back({event.position,event.position,settings.values["drawing"]["shape"].get<std::string>()});m_drawing=true;}
        if(event.type==input::EventType::Up&&m_drawing){m_boxes.back().end=event.position;m_drawing=false;}
    }
    if (!DrawingActive(settings)&&event.type == input::EventType::Down && event.key >= input::Key::MouseLeft && event.key <= input::Key::MouseMiddle &&
        settings.values["cursor"]["click_ripple"].get<bool>()) {
        const auto key = event.key == input::Key::MouseRight ? "right_color" : "left_color";
        auto color = Hex(settings.values["cursor"][key].get<std::string>());
        if (event.key == input::Key::MouseMiddle) color = {.9f,.92f,.95f,1};
        m_ripples.push_back({event.position,color,event.time});
        while (m_ripples.size()>32) m_ripples.pop_front();
    }
}
void OverlayScene::Update(double now, Point cursor, const SettingsModel& settings) {
    m_cursor=cursor;
    if(!DrawingActive(settings)){m_boxes.clear();m_drawing=false;}
    else if(m_drawing&&!m_boxes.empty())m_boxes.back().end=cursor;
    const auto& input = settings.values["input"];
    m_input.Expire(now,(input["hold_ms"].get<double>()+input["fade_ms"].get<double>())/1000);
    while (!m_ripples.empty() && now-m_ripples.front().born>=.32) m_ripples.pop_front();
    if (!settings.values["cursor"]["click_ripple"].get<bool>()) m_ripples.clear();
    const auto& laser=settings.values["laser"];
    const double lifetime=laser["fade_ms"].get<double>()/1000;
    while (!m_laser.empty() && now-m_laser.front().born>=lifetime) m_laser.pop_front();
    const auto hotkey=input::ParseHotkey(laser["hotkey"].get<std::string>());
    const bool active=laser["enabled"].get<bool>() && m_input.Held(hotkey)&&!DrawingActive(settings);
    if(DrawingActive(settings))m_laser.clear();
    if (!laser["enabled"].get<bool>()) m_laser.clear();
    if (active && !m_laserHeld) ++m_stroke;
    m_laserHeld=active;
    if (active && laser["mode"]=="trail") {
        const auto last=m_laser.empty()?cursor:m_laser.back().position;
        if (m_laser.empty() || m_laser.back().stroke!=m_stroke || std::hypot(cursor.x-last.x,cursor.y-last.y)>=1.5f || now-m_laser.back().born>=.008) {
            m_laser.push_back({cursor,now,m_stroke});
            while(m_laser.size()>512) m_laser.pop_front();
        }
    }
    if (laser["mode"]!="trail") m_laser.clear();
}
bool OverlayScene::Animating(double now,const SettingsModel& settings) const {
    const auto& input=settings.values["input"];
    return m_drawing || !m_ripples.empty() || !m_laser.empty() || m_laserHeld ||
        (input["keyboard_enabled"].get<bool>() && !m_input.Chords().empty()) ||
        (input["mouse_enabled"].get<bool>() && (m_input.AnyMouse() || now-m_input.mouseReleased<1 || now-m_input.wheelTime<.75));
}
void OverlayScene::PaintCursor(Painter& painter,Point center,const SettingsModel& settings) {
    const auto& cursor=settings.values["cursor"];
    const float radius=cursor["size"].get<float>()*.5f;
    const float opacity=cursor["opacity"].get<float>();
    auto color=Hex(cursor["color"].get<std::string>(),opacity);
    auto glow=color; glow.a*=cursor["glow"].get<float>();
    painter.Glow(center,radius*1.9f,glow);
    const auto style=cursor["style"].get<std::string>();
    if (style=="ring") painter.Ring(center,radius,cursor["border_width"].get<float>(),color);
    else if (style=="halo") { color.a*=.38f; painter.Glow(center,radius*1.3f,color); painter.Circle(center,radius*.7f,color); }
    else { color.a*=.24f; painter.Circle(center,radius,color); }
}
DrawList OverlayScene::Paint(Rect bounds,float dpi,bool inputMonitor,double now,const SettingsModel& settings) {
    const float width=bounds.width/dpi,height=bounds.height/dpi;
    Painter painter(width,height);
    const auto local=[&](Point p) { return Point{(p.x-bounds.x)/dpi,(p.y-bounds.y)/dpi}; };
    const auto cursor=local(m_cursor);
    if (settings.values["cursor"]["enabled"].get<bool>()) PaintCursor(painter,cursor,settings);
    for(const auto& ripple:m_ripples) {
        const float t=static_cast<float>((now-ripple.born)/.32);
        auto color=ripple.color; color.a=1-Smooth(t);
        painter.Ring(local(ripple.position),11+16*(1-std::pow(1-t,3.f)),2,color);
    }
    const auto& laser=settings.values["laser"];
    auto laserColor=Hex(laser["color"].get<std::string>());
    const float lineWidth=laser["width"].get<float>(), glow=laser["glow"].get<float>();
    const double life=laser["fade_ms"].get<double>()/1000;
    for(size_t i=1;i<m_laser.size();++i) {
        if(m_laser[i-1].stroke!=m_laser[i].stroke) continue;
        auto a=local(m_laser[i-1].position),b=local(m_laser[i].position);
        if(std::hypot(a.x-b.x,a.y-b.y)<.01f) continue;
        const float alpha=Fade(now-m_laser[i-1].born,life*.55,life*.45);
        // Quadratic midpoint interpolation with round joins/caps from the line primitive.
        const auto before=i>1&&m_laser[i-2].stroke==m_laser[i].stroke?local(m_laser[i-2].position):a;
        const auto start=Mix(before,a,.5f),end=Mix(a,b,.5f);
        auto previous=start;
        for(unsigned step=1;step<=2;++step) {
            const float t=step*.5f;
            const auto next=Mix(Mix(start,a,t),Mix(a,end,t),t);
            auto outer=laserColor; outer.a=alpha*glow*.25f;
            painter.Line(previous,next,lineWidth*3,outer);
            auto core=laserColor; core.a=alpha*.95f;
            painter.Line(previous,next,lineWidth,core);
            previous=next;
        }
        if(i+1==m_laser.size()) { auto core=laserColor;core.a=alpha;painter.Line(end,b,lineWidth,core); }
    }
    if(m_laserHeld) {
        auto outer=laserColor;outer.a=glow;
        painter.Glow(cursor,lineWidth*5,outer);
        outer.a=.7f;painter.Circle(cursor,lineWidth*1.2f,outer);
        painter.Circle(cursor,lineWidth*.5f,{1,.96f,.9f,1});
    }
    auto boxColor=Hex(settings.values["drawing"]["color"].get<std::string>());
    const float boxWidth=settings.values["drawing"]["width"].get<float>();
    for(const auto& box:m_boxes){
        const auto a=local(box.start),b=local(box.end);
        PaintAnnotation(painter,a,b,box.shape,boxWidth,boxColor);
    }
    if(!inputMonitor) return painter.List();
    const auto& config=settings.values["input"];
    const float scale=config["scale"].get<float>(),opacity=config["opacity"].get<float>();

    const float x=config["offset_x"].get<float>(),y=config["offset_y"].get<float>();
    const double hold=config["hold_ms"].get<double>()/1000,fade=config["fade_ms"].get<double>()/1000;
    auto accent=Hex(settings.values["general"]["accent"].get<std::string>());
    const float mouseVisible=m_input.AnyMouse()?1:Fade(now-m_input.mouseReleased,hold,fade);
    const float wheel=config["show_wheel"].get<bool>()?Fade(now-m_input.wheelTime,.45,.2):0;
    const float mouseAlpha=config["mouse_enabled"].get<bool>()?std::max(mouseVisible,wheel)*opacity:0;
    size_t visibleRows=0;
    const auto excluded=[&](const input::Chord& chord){
        for(const auto& key:settings.values["exclusions"]["hotkeys"])
            if(chord.combination==input::ParseHotkey(key.get_ref<const std::string&>()))return true;
        return false;
    };
    if(config["keyboard_enabled"].get<bool>()) for(const auto& chord:m_input.Chords())
        if(!excluded(chord)&&(chord.released<0||Fade(now-chord.released,hold,fade)>0))++visibleRows;
    const float groupHeight=static_cast<float>(visibleRows)*42*scale+(mouseAlpha>0?64*scale:0);
    const float gap=std::max(16.f,y);
    float groupY=cursor.y+gap;
    if(groupY+groupHeight>height-8)groupY=cursor.y-gap-groupHeight;
    groupY=std::clamp(groupY,8.f,std::max(8.f,height-groupHeight-8));
    auto followOrigin=[&](float itemWidth,float rowOffset){
        return Point{std::clamp(cursor.x+x-itemWidth*.5f,8.f,std::max(8.f,width-itemWidth-8)),groupY+rowOffset};
    };
    float row=0;
    if(config["keyboard_enabled"].get<bool>()) for(auto item=m_input.Chords().rbegin();item!=m_input.Chords().rend();++item) {
        if(excluded(*item))continue;
        const float alpha=opacity*(item->released<0?1:Fade(now-item->released,hold,fade));
        if(alpha<=0) continue;
        std::vector<std::string> labels;
        const auto& combo=item->combination;
        if(combo.ctrl)labels.emplace_back("Ctrl");if(combo.shift)labels.emplace_back("Shift");
        if(combo.alt)labels.emplace_back("Alt");if(combo.win)labels.emplace_back("Win");
        labels.push_back(input::Label(combo.primary));
        std::vector<text::Layout> text;
        float total=0;
        for(const auto& label:labels) {text.push_back(m_text.Shape(label,12*scale,dpi));total+=std::max(34*scale,text.back().width+20*scale)+6*scale;}
        total-=6*scale;
        auto origin=followOrigin(total,row*42*scale);
        for(size_t i=0;i<labels.size();++i) {
            const float keyWidth=std::max(34*scale,text[i].width+20*scale);
            painter.RoundedRect({origin.x,origin.y,keyWidth,34*scale},7*scale,{.09f,.105f,.13f,alpha});
            painter.Text(text[i],{origin.x+(keyWidth-text[i].width)*.5f,origin.y+(34*scale-text[i].height)*.5f},{.95f,.97f,1,alpha});
            origin.x+=keyWidth+6*scale;
        }
        ++row;
    }
    if(config["mouse_enabled"].get<bool>()) {
        const float alpha=mouseAlpha;
        if(alpha>0) {
            auto origin=followOrigin(48*scale,row*42*scale);
            painter.RoundedRect({origin.x,origin.y,48*scale,64*scale},16*scale,{.09f,.105f,.13f,alpha});
            auto highlight=accent;highlight.a=alpha;
            if(m_input.Down(input::Key::MouseLeft))painter.RoundedRect({origin.x+4*scale,origin.y+4*scale,18*scale,26*scale},6*scale,highlight);
            if(m_input.Down(input::Key::MouseRight))painter.RoundedRect({origin.x+26*scale,origin.y+4*scale,18*scale,26*scale},6*scale,highlight);
            auto scroll=highlight;scroll.a*=m_input.Down(input::Key::MouseMiddle)?1:std::max(.35f,wheel);
            painter.RoundedRect({origin.x+21*scale,origin.y+9*scale,6*scale,15*scale},3*scale,scroll);
            if(wheel>0) {
                // Geometry, not a font glyph: both directions share the same centered bounds.
                const float wheelAlpha=wheel*opacity;
                auto badge=accent;badge.a=wheelAlpha*.28f;
                painter.RoundedRect({origin.x+10*scale,origin.y+31*scale,28*scale,28*scale},8*scale,badge);
                const Point center{origin.x+24*scale,origin.y+45*scale};
                const float direction=m_input.wheelDirection>0?-1.f:1.f;
                const Point tip{center.x,center.y+direction*7*scale};
                const Point tail{center.x,center.y-direction*7*scale};
                const Color ink{.98f,.99f,1.f,wheelAlpha};
                painter.Line(tail,tip,3*scale,ink);
                painter.Line({center.x-6*scale,tip.y-direction*6*scale},tip,3*scale,ink);
                painter.Line({center.x+6*scale,tip.y-direction*6*scale},tip,3*scale,ink);
            }
            if(config["show_side_buttons"].get<bool>()) {
                if(m_input.Down(input::Key::Mouse4))painter.RoundedRect({origin.x-4*scale,origin.y+32*scale,5*scale,10*scale},2*scale,highlight);
                if(m_input.Down(input::Key::Mouse5))painter.RoundedRect({origin.x-4*scale,origin.y+46*scale,5*scale,10*scale},2*scale,highlight);
            }
        }
    }
    return painter.List();
}
void OverlayScene::PaintAnnotation(Painter& painter,Point a,Point b,std::string_view shape,float stroke,Color color){
    const float left=std::min(a.x,b.x),top=std::min(a.y,b.y),right=std::max(a.x,b.x),bottom=std::max(a.y,b.y);
    if(shape=="arrow"){
        const float length=std::hypot(b.x-a.x,b.y-a.y);if(length<2)return;
        const float ux=(b.x-a.x)/length,uy=(b.y-a.y)/length,head=std::min(length*.4f,12.f+stroke*2);
        painter.Line(a,b,stroke,color);
        painter.Line(b,{b.x-ux*head-uy*head*.55f,b.y-uy*head+ux*head*.55f},stroke,color);
        painter.Line(b,{b.x-ux*head+uy*head*.55f,b.y-uy*head-ux*head*.55f},stroke,color);return;
    }
    if(right-left<1||bottom-top<1)return;
    if(shape=="ellipse"){
        const Point center{(left+right)*.5f,(top+bottom)*.5f};const float rx=(right-left)*.5f,ry=(bottom-top)*.5f;
        Point previous{right,center.y};
        for(int n=1;n<=48;++n){float angle=n*6.2831853f/48;Point next{center.x+rx*std::cos(angle),center.y+ry*std::sin(angle)};painter.Line(previous,next,stroke,color);previous=next;}
    }else{
        auto fill=color;fill.a=.06f;painter.FillRect({left,top,right-left,bottom-top},fill);
        painter.Line({left,top},{right,top},stroke,color);painter.Line({right,top},{right,bottom},stroke,color);
        painter.Line({right,bottom},{left,bottom},stroke,color);painter.Line({left,bottom},{left,top},stroke,color);
    }
}
}


