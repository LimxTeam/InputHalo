#pragma once
#include <vector>
#include <string>
namespace ih::ui {
enum class Kind { Toggle, Slider, Combo, Color, Hotkey };
struct Control { const char* label; const char* help; const char* section; const char* key; Kind kind; double low=0,high=1,step=.01; std::vector<std::string> choices; };
inline constexpr const char* Pages[]={"General","Input","Cursor","Laser","Annotations","Presets","Exclusions","About"};
inline constexpr const char* Descriptions[]={"Your presentation, your preferences.","Make shortcuts and mouse actions easy to follow.","Keep your audience focused on the pointer.","Hold your shortcut to point or draw on screen.","Hold to draw. Right-click to undo. Release to clear.","Save, switch and share your presentation setup.","Choose where overlays appear and which shortcuts stay hidden.","Product, ownership and third-party acknowledgements."};
inline std::vector<Control> Controls(int page) {
using enum Kind;
if(page>=5)return {};
switch(page) {
case 0:return {
{"Language","Choose your interface language.","general","language",Combo,0,1,.01,{"zh-CN","en"}},
{"Run at startup","Start InputHalo when you sign in.","general","run_at_startup",Toggle},
{"Start minimized","Keep the settings window out of the way.","general","start_minimized",Toggle},
{"Accent color","Personalize the settings interface.","general","accent",Color}};
case 1:return {
{"Keyboard indicators","Show pressed keys and modifier combinations.","input","keyboard_enabled",Toggle},
{"Shortcuts only","Hide ordinary typing. This is not password detection.","input","shortcut_only",Toggle},
{"Mouse indicators","Show mouse buttons and wheel direction.","input","mouse_enabled",Toggle},
{"Show wheel","Display vertical scrolling.","input","show_wheel",Toggle},
{"Show side buttons","Display Mouse4 and Mouse5.","input","show_side_buttons",Toggle},
{"Horizontal offset","Horizontal offset from the pointer.","input","offset_x",Slider,-500,500,1},
{"Vertical offset","Distance below the pointer.","input","offset_y",Slider,0,500,1},
{"Indicator scale","Resize keyboard and mouse indicators.","input","scale",Slider,.5,3,.05},
{"Opacity","Adjust indicator visibility.","input","opacity",Slider,0,1,.01},
{"Hold time (ms)","How long released keys remain visible.","input","hold_ms",Slider,0,3000,50},
{"Fade time (ms)","How quickly released keys disappear.","input","fade_ms",Slider,0,1500,10}};
case 2:return {
{"Cursor highlight","Follow the real pointer position.","cursor","enabled",Toggle},
{"Style","Choose a ring, halo or soft spot.","cursor","style",Combo,0,1,.01,{"ring","halo","spot"}},
{"Size","Highlight diameter in device independent pixels.","cursor","size",Slider,4,256,1},
{"Color","Set the highlight color.","cursor","color",Color},
{"Opacity","Adjust highlight visibility.","cursor","opacity",Slider,0,1,.01},
{"Border width","Ring stroke width.","cursor","border_width",Slider,0,32,.5},
{"Glow","Soft light around the highlight.","cursor","glow",Slider,0,1,.01},
{"Click ripples","Animate clicks at their original position.","cursor","click_ripple",Toggle},
{"Left click color","Color of left click ripples.","cursor","left_color",Color},
{"Right click color","Color of right click ripples.","cursor","right_color",Color}};
case 3:return {
{"Laser enabled","Activate while holding the chosen shortcut.","laser","enabled",Toggle},
{"Mode","Point precisely or leave a fading trail.","laser","mode",Combo,0,1,.01,{"trail","pointer"}},
{"Activation shortcut","Click to record. Escape cancels. Mouse4/5 supported.","laser","hotkey",Hotkey},
{"Color","Set the laser color.","laser","color",Color},
{"Width","Laser stroke width.","laser","width",Slider,1,64,.5},
{"Glow","Soft light around the laser.","laser","glow",Slider,0,1,.01},
{"Trail lifetime (ms)","How long a trail remains on screen.","laser","fade_ms",Slider,100,5000,50}};
default:return {
{"Box drawing","Hold the shortcut and drag with the left mouse button.","drawing","enabled",Toggle},
{"Shape","Right-click while holding the shortcut to undo the last shape.","drawing","shape",Combo,0,1,.01,{"rectangle","arrow","ellipse"}},
{"Activation shortcut","Hold to keep all boxes. Release to clear them.","drawing","hotkey",Hotkey},
{"Color","Set the box outline color.","drawing","color",Color},
{"Border width","Box outline width.","drawing","width",Slider,1,12,.5}};
}}
inline std::string Display(std::string value) { for(auto& c:value) if(c=='_')c=' '; if(!value.empty()&&value[0]>='a'&&value[0]<='z')value[0]-=32; return value; }
}

