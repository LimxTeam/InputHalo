/*******************************************************************************
 * 文件: Strings.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   声明设置控件的元数据：控件种类枚举 Kind、Control 结构、页面名/描述数组，
 *   以及按页返回控件列表的工厂 Controls(page) 与取值显示格式化 Display()。
 *   不涉及复杂算法，纯数据描述。
 *
 * 设计哲学:
 *   用数据描述 UI：渲染与交互遍历同一份 Control 列表，
 *   新增一个控件只需加一行数据，避免布局逻辑散落各处。
 *
 * 技术特性:
 *   - Kind 含 Toggle/Slider/Combo/Color/Hotkey
 *   - Controls(page) 按页返回控件集合
 *   - Display 把 snake_case 取值转为友好显示
 *
 * 依赖关系:
 *   外部: <vector>、<string>
 *
 * 注意事项:
 *   page>=5 返回空(方案/排除/关于由对应子面板负责)。
 ******************************************************************************/
#pragma once
#include <vector>
#include <string>
// InputHalo UI 层命名空间。
namespace ih::ui {
// 控件种类：开关、滑杆、下拉组合、颜色、快捷键录制。
enum class Kind { Toggle, Slider, Combo, Color, Hotkey };
// 控件描述：标签、帮助文案、设置分区/键、种类、滑杆范围与步长、组合选项。
struct Control { const char* label; const char* help; const char* section; const char* key; Kind kind; double low=0,high=1,step=.01; std::vector<std::string> choices; };
// 八个设置页的英文标题。
inline constexpr const char* Pages[]={"General","Input","Cursor","Laser","Annotations","Presets","Exclusions","About"};
// 每个页面对应的描述文案。
inline constexpr const char* Descriptions[]={"Your presentation, your preferences.","Make shortcuts and mouse actions easy to follow.","Keep your audience focused on the pointer.","Hold your shortcut to point or draw on screen.","Hold to draw. Right-click to undo. Release to clear.","Save, switch and share your presentation setup.","Choose where overlays appear and which shortcuts stay hidden.","Product, ownership and third-party acknowledgements."};
// 按页返回该页所有控件元数据。
inline std::vector<Control> Controls(int page) {
using enum Kind;
// 第 5 页及以后由子面板(方案/排除/关于)渲染，这里返回空。
if(page>=5)return {};
switch(page) {
// 常规页：语言、开机启动、启动收起、主题色。
case 0:return {
{"Language","Choose your interface language.","general","language",Combo,0,1,.01,{"zh-CN","en"}},
{"Run at startup","Start InputHalo when you sign in.","general","run_at_startup",Toggle},
{"Start minimized","Keep the settings window out of the way.","general","start_minimized",Toggle},
{"Accent color","Personalize the settings interface.","general","accent",Color}};
// 操作提示页：键盘/鼠标提示与时间偏移等滑杆。
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
// 光标页：高亮开关、样式、尺寸、颜色与波纹。
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
// 激光页：开关、模式、快捷键、颜色与轨迹时间。
case 3:return {
{"Laser enabled","Activate while holding the chosen shortcut.","laser","enabled",Toggle},
{"Mode","Point precisely or leave a fading trail.","laser","mode",Combo,0,1,.01,{"trail","pointer"}},
{"Activation shortcut","Click to record. Escape cancels. Mouse4/5 supported.","laser","hotkey",Hotkey},
{"Color","Set the laser color.","laser","color",Color},
{"Width","Laser stroke width.","laser","width",Slider,1,64,.5},
{"Glow","Soft light around the laser.","laser","glow",Slider,0,1,.01},
{"Trail lifetime (ms)","How long a trail remains on screen.","laser","fade_ms",Slider,100,5000,50}};
// 标注页：开关、形状、快捷键、颜色与线宽。
default:return {
{"Box drawing","Hold the shortcut and drag with the left mouse button.","drawing","enabled",Toggle},
{"Shape","Right-click while holding the shortcut to undo the last shape.","drawing","shape",Combo,0,1,.01,{"rectangle","arrow","ellipse"}},
{"Activation shortcut","Hold to keep all boxes. Release to clear them.","drawing","hotkey",Hotkey},
{"Color","Set the box outline color.","drawing","color",Color},
{"Border width","Box outline width.","drawing","width",Slider,1,12,.5}};
}}
// 把取值显示化：下划线转空格，首字母大写。
inline std::string Display(std::string value) { for(auto& c:value) if(c=='_')c=' '; if(!value.empty()&&value[0]>='a'&&value[0]<='z')value[0]-=32; return value; }
}

