#pragma once
#include "UI/Strings.h"
#include "UI/AboutPanel.h"
#include "UI/LibraryPanel.h"
#include "Core/Events.h"
#include "Features/OverlayScene.h"
namespace ih::ui {
class SettingsPanel {
public:
 explicit SettingsPanel(text::TextEngine& text):m_text(text),m_controls(Controls(0)),m_about(text),m_library(text){}
 void Resize(float width,float height,float scale);
 void Chrome(float scale,bool maximized,int hover){m_chromeScale=scale;m_maximized=maximized;m_chromeHover=hover;}
 void Event(UiEvent event,SettingsModel& settings,double now);
 void Raw(const input::Event& event,SettingsModel& settings);
 render2d::DrawList Paint(const SettingsModel& settings,double now);
 bool Recording()const{return m_recording||m_library.Recording();}
 bool Changed(){const bool library=m_library.Changed();return std::exchange(m_changed,false)||library;}
 LibraryPanel& Library(){return m_library;}
 void MarkChanged(){m_changed=true;}
 bool Enabled()const{return m_enabled;}
 void ToggleEnabled(){m_enabled=!m_enabled;m_changed=true;}
 bool Animating(double now)const{return now<m_motionUntil;}
 void ReduceMotion(bool value){m_reduceMotion=value;}
 std::string status="Changes save automatically";
private:
 render2d::Rect Row(int index)const;
 render2d::Rect Widget(int index)const;
 void Activate(int index,SettingsModel& settings,double now);
 void Adjust(int index,double direction,SettingsModel& settings);
 void Drag(float x,float y,SettingsModel& settings);
 float ContentHeight()const;
 float ContentTop()const{return m_page==0?88.f:180.f;}
 render2d::Rect Popup()const;
 void EnsureVisible();
 text::TextEngine& m_text;
 std::vector<Control> m_controls;
 float m_width=960,m_height=640,m_scale=1,m_scroll=0;
 int m_page=0,m_focus=-1,m_hover=-1,m_drag=-1,m_popup=-1,m_choice=0,m_colorChannel=-1;
 bool m_recording=false,m_changed=false,m_enabled=true,m_reduceMotion=false;
 double m_motionUntil=0;
 input::State m_recordState;
 float m_chromeScale=1;
 bool m_maximized=false;
 int m_chromeHover=-1;
 float m_dragY=0;double m_dragValue=0;
 double m_previewStart=-10;
 bool m_colorDragging=false;
 bool m_linearDrag=false;
 int m_recordTarget=-1;
 AboutPanel m_about;
 LibraryPanel m_library;
};
}
