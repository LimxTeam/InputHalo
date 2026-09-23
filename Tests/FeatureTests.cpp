#include "Features/OverlayScene.h"
#include "Platform/Windows/Win32Input.h"
#include "Platform/Windows/Win32Fonts.h"
#include "UI/SettingsPanel.h"
#include <iostream>
#include <stdexcept>
#include <cmath>
using namespace ih;using namespace ih::input;
void Require(bool v,const char* message){if(!v)throw std::runtime_error(message);}
int main(){try{
 for(unsigned n=1;n<KeyCount;++n){auto key=static_cast<Key>(n);if(IsModifier(key))continue;Hotkey h{true,true,true,true,key};Require(ParseHotkey(FormatHotkey(h))==h,"Hotkey round trip");}
 Require(!ParseHotkey("Ctrl+Unknown").Valid(),"Reject unknown key");Require(!ParseHotkey("Ctrl+A+B").Valid(),"Reject multiple primary keys");Require(ReservedHotkey(ParseHotkey("Alt+F4")),"Reserved shortcut");
 Require(platform::MapKey(VK_RETURN,0x1c,RI_KEY_E0)==Key::NumpadEnter,"Extended enter");Require(platform::MapKey(VK_CONTROL,0x1d,RI_KEY_E0)==Key::RightCtrl,"Right ctrl");Require(platform::MapKey(VK_SHIFT,0x36,0)==Key::RightShift,"Right shift");Require(platform::MapKey(VK_LEFT,0x4b,0)==Key::Numpad4,"Numlock off mapping");Require(platform::MapKey(VK_LEFT,0x4b,RI_KEY_E0)==Key::Left,"Navigation mapping");
 State state;state.Apply({EventType::Down,Key::A,0,0},true);Require(state.Chords().empty(),"Ordinary typing hidden");state.Apply({EventType::Up,Key::A,0,.01},true);state.Apply({EventType::Down,Key::LeftCtrl,0,.1},true);state.Apply({EventType::Down,Key::A,0,.2},true);state.Apply({EventType::Down,Key::A,0,.3},true);Require(state.Chords().size()==1&&state.Chords()[0].repeats==2,"Autorepeat aggregation");state.Apply({EventType::Up,Key::A,0,.4},true);Require(state.Chords()[0].released==.4,"Release starts lifetime");state.Expire(1,.83);Require(state.Chords().size()==1,"Hold survives");state.Expire(1.24,.83);Require(state.Chords().empty(),"History expires");
 for(unsigned n=0;n<100;++n){state.Apply({EventType::Down,static_cast<Key>(Index(Key::A)+n%26),0,2+n*.3},false);}Require(state.Chords().size()<=4,"History bounded");state.Reset();Require(!state.Held(ParseHotkey("Ctrl+A"))&&state.Chords().empty(),"Reset clears input");
 auto anchor=features::Anchor("bottom_right",1000,500,100,50,0,48);Require(anchor.x==876&&anchor.y==402,"Bottom right anchor");anchor=features::Anchor("top_left",100,100,200,200,-500,-500);Require(anchor.x==0&&anchor.y==0,"Clamp oversized anchor");
 text::TextEngine text;for(auto& font:platform::DefaultFontPaths())text.AddFont(font);features::OverlayScene scene(text);auto s=SettingsModel::Defaults();
 scene.Apply({EventType::Down,Key::MouseLeft,0,1,{-100,200}},s);scene.Update(1,{-150,200},s);Require(scene.RippleCount()==1,"Click ripple starts");auto list=scene.Paint({-1920,0,1920,1080},1.5f,true,1,s);Require(!list.commands.empty(),"Negative desktop coordinates draw");scene.Update(1.321,{},s);Require(scene.RippleCount()==0,"Ripple expires at 320 ms");scene.Reset();
 scene.Apply({EventType::Down,Key::LeftCtrl,0,2},s);scene.Apply({EventType::Down,Key::LeftAlt,0,2},s);scene.Apply({EventType::Down,Key::Space,0,2},s);
 for(int i=0;i<10000;++i)scene.Update(2+i*.0001,{static_cast<float>(i%1920),500},s);Require(scene.LaserCount()<=512,"Laser memory bounded");Require(scene.Paint({0,0,1920,1080},1,true,3,s).commands.size()<render2d::MaxCommands,"Laser command capacity");scene.Apply({EventType::Up,Key::Space,0,3},s);scene.Update(4.3,{},s);Require(scene.LaserCount()==0,"Released trail expires");
 ui::SettingsPanel panel(text);panel.Resize(960,640,1);panel.Event({UiEventType::Down,50,280},s,0);panel.Event({UiEventType::Down,800,308},s,0);Require(panel.Recording(),"Hotkey button records");panel.Raw({EventType::Down,Key::Mouse4},s);Require(!panel.Recording()&&s.values["laser"]["hotkey"]=="Mouse4","Side button recording");Require(panel.Changed(),"Settings dirty");
 for(int page=0;page<4;++page){panel.Event({UiEventType::Down,50,140+page*48.f},s,1);for(int n=0;n<25;++n)panel.Event({UiEventType::KeyDown,0,0,0,Key::Tab},s,1);auto ui=panel.Paint(s,1);Require(ui.commands.size()<render2d::MaxCommands,"UI command capacity");}
 std::cout<<"Input, features, mapping and UI tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
