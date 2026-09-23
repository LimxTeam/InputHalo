#include "UI/LibraryPanel.h"
#include "UI/Localization.h"
#include "Render2D/Color.h"
#include <algorithm>
namespace ih::ui {
using namespace render2d;
void LibraryPanel::Run(int command,SettingsModel& settings){
    if(command>=1000){
        const size_t index=static_cast<size_t>(command-1000);auto& list=settings.values["exclusions"][m_tab==0?"apps":"hotkeys"];
        if(index<list.size()){list.erase(index);m_changed=true;}return;
    }
    if(command>=100){m_selected=command-100;m_confirmDelete=false;return;}
    if(command==10||command==11){m_tab=command-10;m_scroll=0;m_focus=-1;return;}
    if(command==12){m_action=LibraryAction::AddApplication;return;}
    if(command==13){m_recording=true;m_recordState.Reset();status="Press the shortcut to hide; Escape cancels";return;}
    if(command==1)m_action=LibraryAction::SaveProfile;
    if(command==2)m_action=LibraryAction::ApplyProfile;
    if(command==3){if(!m_confirmDelete){m_confirmDelete=true;return;}m_action=LibraryAction::DeleteProfile;m_confirmDelete=false;}
    if(command==4)m_action=LibraryAction::ImportProfile;
    if(command==5)m_action=LibraryAction::ExportProfile;
}
void LibraryPanel::Event(const UiEvent& e,SettingsModel& settings,bool){
    if(e.type==UiEventType::Blur){Cancel();return;}
    if(m_recording)return;
    if(e.type==UiEventType::Wheel){m_scroll=std::clamp(m_scroll-e.wheel*48,0.f,m_maxScroll);return;}
    if(e.type==UiEventType::Down){
        for(size_t i=0;i<m_buttons.size();++i){const auto& b=m_buttons[i];const auto r=b.bounds;if(b.enabled&&e.x>=r.x&&e.x<r.x+r.width&&e.y>=r.y&&e.y<r.y+r.height){m_focus=static_cast<int>(i);Run(b.command,settings);return;}}
        m_confirmDelete=false;
    }
    if(e.type==UiEventType::KeyDown){
        if(e.key==input::Key::Escape){Cancel();return;}
        if(e.key==input::Key::Tab&&!m_buttons.empty())m_focus=(m_focus+(e.shift?-1:1)+static_cast<int>(m_buttons.size()))%static_cast<int>(m_buttons.size());
        if((e.key==input::Key::Enter||e.key==input::Key::Space)&&m_focus>=0&&m_focus<static_cast<int>(m_buttons.size())&&m_buttons[m_focus].enabled)Run(m_buttons[m_focus].command,settings);
        if(e.key==input::Key::PageDown||e.key==input::Key::PageUp)m_scroll=std::clamp(m_scroll+(e.key==input::Key::PageDown?200:-200),0.f,m_maxScroll);
    }
}
void LibraryPanel::Raw(const input::Event& e,SettingsModel& settings){
    if(!m_recording)return;m_recordState.Apply(e,false);
    if(e.type==input::EventType::Reset||(e.type==input::EventType::Down&&e.key==input::Key::Escape)){m_recording=false;status="Shortcut recording cancelled";return;}
    if(e.type!=input::EventType::Down||input::IsModifier(e.key)||e.key>=input::Key::MouseLeft)return;
    const auto key=m_recordState.Modifiers(e.key);if(!key.Valid())return;
    const auto name=input::FormatHotkey(key);auto& keys=settings.values["exclusions"]["hotkeys"];
    if(std::find(keys.begin(),keys.end(),name)==keys.end()&&keys.size()<128){keys.push_back(name);m_changed=true;}
    m_recording=false;status="Shortcut hidden; its original action still works";
}
void LibraryPanel::Paint(Painter& p,float width,float height,float dpi,bool chinese,const SettingsModel& settings,bool exclusions){
    m_buttons.clear();const auto white=Hex("#E8ECF1"),muted=Hex("#95A0B1");
    auto text=[&](std::string_view value,float x,float y,float size=11){p.Text(m_text.Shape(Translate(value,chinese),size,dpi),{x,y},white);};
    auto button=[&](Rect r,std::string label,int command,bool enabled=true,bool selected=false){
        const bool focused=m_focus==static_cast<int>(m_buttons.size());m_buttons.push_back({r,label,command,enabled});
        p.RoundedRect(r,4,Hex(selected?"#48405F":focused?"#3A4355":"#283140"));p.PushClip(r);p.Text(m_text.Shape(Translate(label,chinese),11,dpi),{r.x+8,r.y+8},enabled?white:muted);p.PopClip();
    };
    float top=156;size_t count{};
    if(exclusions){
        button({176,78,142,28},"Applications",10,true,m_tab==0);button({324,78,160,28},"Hidden shortcuts",11,true,m_tab==1);
        button({176,116,170,28},m_tab==0?"Add application...":m_recording?"Press shortcut...":"Record shortcut",m_tab==0?12:13,settings.values["exclusions"][m_tab==0?"apps":"hotkeys"].size()<128);
        count=settings.values["exclusions"][m_tab==0?"apps":"hotkeys"].size();
    }else{
        const bool selected=m_selected>=0&&m_selected<static_cast<int>(profiles.size());
        const float segment=(width-192)/5;
        const char* names[]={"Save current...","Apply","Delete","Import...","Export..."};
        for(int i=0;i<5;++i)button({176+i*segment,78,segment-5,28},i==2&&m_confirmDelete?"Confirm delete":names[i],i+1,i==0||i==3||(selected&&(i!=2||!profiles[m_selected].builtin)));
        text("Presets keep your language, startup and exclusions",180,120,10);count=profiles.size();
    }
    const float available=std::clamp(height-top-76,40.f,450.f);m_maxScroll=std::max(0.f,count*38.f-available);m_scroll=std::clamp(m_scroll,0.f,m_maxScroll);
    p.PushClip({176,top,width-192,available});
    for(size_t i=0;i<count;++i){const float y=top+i*38.f-m_scroll;if(y<top||y+32>top+available)continue;
        if(exclusions){
            const auto label=settings.values["exclusions"][m_tab==0?"apps":"hotkeys"][i].get<std::string>();
            p.PushClip({180,y,width-290,32});text(label,180,y+8);p.PopClip();button({width-98,y,78,28},"Remove",1000+static_cast<int>(i));
        }else button({176,y,width-192,32},profiles[i].name,100+static_cast<int>(i),true,m_selected==static_cast<int>(i));
    }
    if(!count)text("Nothing excluded",180,top+10);
    p.PopClip();
    if(m_maxScroll>0){const float thumb=available*available/(count*38.f);p.RoundedRect({width-9,top+m_scroll*available/(count*38.f),2,thumb},1,muted);}
    p.PushClip({176,height-68,width-192,66});
    if(exclusions){text(m_tab==0?"Matching foreground apps pause all overlays":"Only hides exact key combinations; does not block keys",180,height-62,10);}
    text(status,180,height-26,10);p.PopClip();
}
}
