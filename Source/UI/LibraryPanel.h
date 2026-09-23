#pragma once
#include "Core/Events.h"
#include "Settings/ProfileStore.h"
#include "Input/InputState.h"
#include "Text/TextEngine.h"
#include "Render2D/Painter.h"
#include <utility>
namespace ih::ui {
enum class LibraryAction {None,SaveProfile,ApplyProfile,DeleteProfile,ImportProfile,ExportProfile,AddApplication};
class LibraryPanel {
public:
    explicit LibraryPanel(text::TextEngine& text):m_text(text){}
    std::vector<Profile> profiles;
    std::string status="Select a preset to apply or export";
    void Event(const UiEvent& event,SettingsModel& settings,bool exclusions);
    void Raw(const input::Event& event,SettingsModel& settings);
    void Paint(render2d::Painter& painter,float width,float height,float dpi,bool chinese,const SettingsModel& settings,bool exclusions);
    void Cancel(){m_recording=false;m_confirmDelete=false;m_focus=-1;}
    bool Recording()const{return m_recording;}
    bool Changed(){return std::exchange(m_changed,false);}
    LibraryAction TakeAction(){return std::exchange(m_action,LibraryAction::None);}
    int Selected()const{return m_selected;}
    void Select(int value){m_selected=value;}
private:
    struct Button {render2d::Rect bounds;std::string label;int command;bool enabled;};
    void Run(int command,SettingsModel& settings);
    text::TextEngine& m_text;
    std::vector<Button> m_buttons;
    input::State m_recordState;
    LibraryAction m_action=LibraryAction::None;
    int m_selected=0,m_tab=0,m_focus=-1;
    float m_scroll=0,m_maxScroll=0;
    bool m_recording=false,m_changed=false,m_confirmDelete=false;
};
}
