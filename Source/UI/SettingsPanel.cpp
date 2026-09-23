/*******************************************************************************
 * 文件: SettingsPanel.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   实现设置面板的布局几何、激活/调节/拖动、事件路由、快捷键录制校验与完整绘制：
 *   侧边栏、实时预览、各类控件、颜色/组合弹出层，以及与内容合并的窗口标题栏 chrome。
 *   复杂度: O(控件数) 的线性命中与绘制。
 *
 * 设计哲学:
 *   元数据驱动：Control 描述 label/section/key/kind/范围，Event 与 Paint 共用同一套 Row/Widget 几何，
 *   杜绝布局与交互漂移；所有改动写回 SettingsModel 并置变更标记。
 *
 * 技术特性:
 *   - Slider 旋钮角度 + 线性轨道双手势
 *   - 颜色弹出支持 RGB 通道拖动与纯键盘导航
 *   - 预览脉冲动画；最后把内容与标题栏合并为单一 DrawList
 *
 * 依赖关系:
 *   内部: UI/Localization.h、UI/Brand.h、ProductInfo.h、Features/OverlayScene.h
 *   外部: render2d、input、<format>
 *
 * 注意事项:
 *   弹出层坐标 clamp 在客户区内；标题栏偏移在合并命令时统一叠加 titleHeight。
 ******************************************************************************/
#include "UI/SettingsPanel.h"
#include "UI/Localization.h"
#include "UI/Brand.h"
#include "ProductInfo.h"
#include "Render2D/Color.h"
#include <algorithm>
#include <cmath>
#include <format>
// InputHalo UI 层命名空间实现。
namespace ih::ui {
using namespace render2d;
// 命中测试 lambda：点是否在矩形内。
namespace { bool Inside(Rect r,float x,float y){return x>=r.x&&y>=r.y&&x<r.x+r.width&&y<r.y+r.height;} }
// 尺寸变化：收敛滚动使内容不越界。
void SettingsPanel::Resize(float w,float h,float scale){m_width=w;m_height=h;m_scale=scale;m_scroll=std::clamp(m_scroll,0.f,std::max(0.f,ContentHeight()-(h-ContentTop()-28)));}
// 行矩形：内容顶部起每行 44px。
Rect SettingsPanel::Row(int index)const{return {176,ContentTop()+index*44.f-m_scroll,m_width-192,42};}
// 内容总高度：取最后一行底边。
float SettingsPanel::ContentHeight()const{float bottom=ContentTop();for(int i=0;i<static_cast<int>(m_controls.size());++i){auto r=Row(i);bottom=std::max(bottom,r.y+m_scroll+r.height);}return bottom-ContentTop();}
// 控件 widget 矩形：按控件类型决定宽度并右对齐。
Rect SettingsPanel::Widget(int index)const{
 auto r=Row(index);const auto kind=m_controls[index].kind;
 const float width=kind==Kind::Toggle?36.f:std::min(kind==Kind::Color?240.f:kind==Kind::Combo?260.f:220.f,r.width*.52f);
 return {r.x+r.width-width-10,r.y+7,width,28};
}
// 弹出菜单矩形：限制在客户区内。
Rect SettingsPanel::Popup()const{auto w=Widget(m_popup);return {std::clamp(w.x,176.f,std::max(176.f,m_width-314)),std::clamp(w.y,104.f,std::max(104.f,m_height-270)),290,230};}
// 焦点行不可见时自动滚动使其可见。
void SettingsPanel::EnsureVisible(){if(m_focus<0)return;auto r=Row(m_focus);if(r.y<ContentTop())m_scroll-=ContentTop()-r.y;if(r.y+r.height>m_height-32)m_scroll+=r.y+r.height-(m_height-32);Resize(m_width,m_height,m_scale);}
// 激活控件：按种类切换/循环/开弹出/进录制。
void SettingsPanel::Activate(int i,SettingsModel& s,double now){
 if(i<0)return;auto& c=m_controls[i];auto& v=s.values[c.section][c.key];
// Toggle：取反布尔。
 if(c.kind==Kind::Toggle){v=!v.get<bool>();m_changed=true;}
// Combo：循环到下一个选项。
 else if(c.kind==Kind::Combo){size_t n=0;for(;n<c.choices.size();++n)if(c.choices[n]==v.get<std::string>())break;v=c.choices[(n+1)%c.choices.size()];m_changed=true;}
// Color：打开颜色弹出层。
 else if(c.kind==Kind::Color){m_popup=i;m_colorChannel=0;}
// Hotkey：进入录制态。
 else if(c.kind==Kind::Hotkey){m_recordTarget=i;m_recording=true;m_recordState.Reset();status="Press a shortcut; Escape cancels";}
// 记录动画截止时间(可减弱)。
 m_motionUntil=now+(m_reduceMotion?0:.14);
}
// 滑杆调整：按方向加/减步长并钳制。
void SettingsPanel::Adjust(int i,double direction,SettingsModel& s){if(i<0)return;auto& c=m_controls[i];if(c.kind!=Kind::Slider)return;auto& v=s.values[c.section][c.key];v=std::clamp(v.get<double>()+direction*c.step,c.low,c.high);m_changed=true;}
// 拖动处理：支持旋钮垂直拖动与轨道线性拖动两种手势。
void SettingsPanel::Drag(float x,float y,SettingsModel& s){
 if(m_drag<0)return;auto& c=m_controls[m_drag];auto w=Widget(m_drag);
// 由指针位置反推目标值。
 const double value=m_linearDrag?c.low+std::clamp((x-w.x-38)/(w.width-106),0.f,1.f)*(c.high-c.low):m_dragValue+(m_dragY-y)*(c.high-c.low)/180.;
// 按步长取整并钳制。
 s.values[c.section][c.key]=std::clamp(std::round(value/c.step)*c.step,c.low,c.high);m_changed=true;
}
// 原始输入事件：优先转发给 library 录制，否则处理本面板热键录制。
void SettingsPanel::Raw(const input::Event& e,SettingsModel& s){
 if(m_library.Recording()){m_library.Raw(e,s);return;}if(!m_recording)return;m_recordState.Apply(e,false);
// Reset 取消录制。
 if(e.type==input::EventType::Reset){m_recording=false;status="Shortcut recording cancelled";return;}
 if(e.type!=input::EventType::Down||input::IsModifier(e.key))return;
// Escape 取消录制。
 if(e.key==input::Key::Escape){m_recording=false;status="Shortcut recording cancelled";return;}
 if(e.key>=input::Key::MouseLeft&&e.key<=input::Key::MouseMiddle)return;
// 合成热键组合。
 auto key=m_recordState.Modifiers(e.key);
// 非法或被 Windows 保留则提示并拒绝。
 if(!key.Valid()||input::ReservedHotkey(key)){status="That shortcut is reserved by Windows";return;}
 const auto& target=m_controls[m_recordTarget];
// 与另一工具(drawing/laser)互斥检查。
 const char* other=std::string_view(target.section)=="drawing"?"laser":"drawing";
// 撞键则提示已被占用。
 if(key==input::ParseHotkey(s.values[other]["hotkey"].get<std::string>())){status="Shortcut already used by another tool";return;}
// 写入新热键并标记变更。
 s.values[target.section][target.key]=input::FormatHotkey(key);m_changed=true;m_recording=false;status="Shortcut updated";
}
// 主事件路由。
void SettingsPanel::Event(UiEvent e,SettingsModel& s,double now){
// 事件坐标先除以缩放还原逻辑坐标。
 e.x/=m_scale;e.y/=m_scale;
// 失焦清理所有临时状态。
 if(e.type==UiEventType::Blur){m_drag=-1;m_popup=-1;m_recording=false;m_colorDragging=false;m_library.Cancel();return;}
// 录制态：仅响应侧栏点击以退出录制。
 if(Recording()){
  if(e.type==UiEventType::Down&&e.x<160){m_recording=false;m_library.Cancel();}
  else return;
 }
// 方案/排除页：事件转发给 library 子面板。
 if(m_page==5||m_page==6){
  if(e.type==UiEventType::KeyDown&&e.key==input::Key::Escape){m_library.Cancel();m_focus=-m_page-2;return;}
  const bool sidebarKey=m_focus<-1&&e.type==UiEventType::KeyDown&&(e.key==input::Key::Tab||e.key==input::Key::Enter||e.key==input::Key::Space);
  if(!(e.type==UiEventType::Down&&e.x<160)&&!sidebarKey){if(e.type==UiEventType::Down)m_focus=-1;m_library.Event(e,s,m_page==6);return;}
 }
// 关于页：事件转发给 about 子面板(侧栏导航除外)。
 if(m_page==7){
  const bool sidebarClick=e.type==UiEventType::Down&&e.x<160;
  const bool navigation=e.type==UiEventType::KeyDown&&(e.key==input::Key::Tab||e.key==input::Key::Enter||e.key==input::Key::Space);
  if(!sidebarClick&&!navigation){m_about.Event(e);return;}
 }
// 弹出层打开时：优先处理弹出层内的键盘/拖动/点击。
 if(m_popup>=0){auto& c=m_controls[m_popup];
  const Rect box=Popup();
// 颜色修改 lambda：改某 RGB 通道并写回 #RRGGBB。
  auto colorChange=[&](int channel,float value){auto color=Hex(s.values[c.section][c.key].get<std::string>());float rgb[]={color.r,color.g,color.b};rgb[channel]=std::clamp(value,0.f,1.f);s.values[c.section][c.key]=std::format("#{:02X}{:02X}{:02X}",static_cast<int>(std::round(rgb[0]*255)),static_cast<int>(std::round(rgb[1]*255)),static_cast<int>(std::round(rgb[2]*255)));m_changed=true;};
  if(e.type==UiEventType::KeyDown){
   if(e.key==input::Key::Escape){m_popup=-1;return;}
   if(c.kind==Kind::Combo){if(e.key==input::Key::Down)m_choice=(m_choice+1)%static_cast<int>(c.choices.size());if(e.key==input::Key::Up)m_choice=(m_choice+static_cast<int>(c.choices.size())-1)%static_cast<int>(c.choices.size());if(e.key==input::Key::Enter||e.key==input::Key::Space){s.values[c.section][c.key]=c.choices[m_choice];m_changed=true;m_popup=-1;}}
   else{if(e.key==input::Key::Tab)m_colorChannel=(m_colorChannel+1)%3;if(e.key==input::Key::Left||e.key==input::Key::Right){auto color=Hex(s.values[c.section][c.key].get<std::string>());float rgb[]={color.r,color.g,color.b};colorChange(m_colorChannel,rgb[m_colorChannel]+(e.key==input::Key::Left?-1.f:1.f)/255);}if(e.key==input::Key::Enter)m_popup=-1;}
  }
  if(e.type==UiEventType::Up)m_colorDragging=false;
  if(e.type==UiEventType::Move&&m_colorDragging){colorChange(m_colorChannel,(e.x-box.x-46)/224);return;}
  if(e.type==UiEventType::Down){if(!Inside(box,e.x,e.y)){m_popup=-1;return;}if(c.kind==Kind::Combo){int n=static_cast<int>((e.y-box.y-8)/32);if(n>=0&&n<static_cast<int>(c.choices.size())){s.values[c.section][c.key]=c.choices[n];m_changed=true;m_popup=-1;}}else{int n=static_cast<int>((e.y-box.y-60)/44);if(n>=0&&n<3){m_colorChannel=n;m_colorDragging=true;colorChange(n,(e.x-box.x-46)/224);}}}
  return;
 }
// 滚轮：悬停滑杆则调值，否则滚动页面。
 if(e.type==UiEventType::Wheel){
  if(m_focus>=0&&m_controls[m_focus].kind==Kind::Slider&&Inside(Widget(m_focus),e.x,e.y)){Adjust(m_focus,e.wheel,s);return;}
  m_scroll-=e.wheel*56;Resize(m_width,m_height,m_scale);return;
 }
// 松开结束拖动。
 if(e.type==UiEventType::Up)m_drag=-1;
// 移动：更新悬停行并继续拖动。
 if(e.type==UiEventType::Move){m_hover=-1;for(int i=0;i<static_cast<int>(m_controls.size());++i)if(e.y>=ContentTop()&&e.y<m_height-32&&Inside(Row(i),e.x,e.y))m_hover=i;if(m_drag>=0)Drag(e.x,e.y,s);}
// 按下处理。
 if(e.type==UiEventType::Down){
// 点击实时预览区则重播动画。
  if(m_page>0&&m_page<5&&Inside({176,76,m_width-192,76},e.x,e.y)){m_previewStart=now;m_motionUntil=now+(m_reduceMotion?0:.7);return;}
// 点击侧边栏切换页面。
  for(int p=0;p<8;++p)if(Inside({12,104+p*38.f,136,32},e.x,e.y)){m_library.Cancel();m_page=p;m_controls=Controls(p);m_focus=-1;m_scroll=0;m_hover=-1;m_previewStart=now;m_motionUntil=now+(m_reduceMotion?0:.7);return;}
// 点击底部开关切换覆盖层启用。
  if(Inside({12,m_height-60,136,32},e.x,e.y)){ToggleEnabled();return;}
// 点击某控件行：按种类决定拖动/循环/快捷选色/激活。
  for(int i=0;i<static_cast<int>(m_controls.size());++i)if(e.y>=ContentTop()&&e.y<m_height-32&&Inside(Row(i),e.x,e.y)){
   m_focus=i;auto& c=m_controls[i];auto w=Widget(i);
   if(c.kind==Kind::Slider&&Inside(w,e.x,e.y)){m_drag=i;m_dragY=e.y;m_dragValue=s.values[c.section][c.key].get<double>();m_linearDrag=e.x>w.x+34;if(m_linearDrag)Drag(e.x,e.y,s);}
   else if(c.kind==Kind::Combo&&Inside(w,e.x,e.y)){int n=std::clamp(static_cast<int>((e.x-w.x)/((w.width+6)/c.choices.size())),0,static_cast<int>(c.choices.size())-1);s.values[c.section][c.key]=c.choices[n];m_changed=true;}
   else if(c.kind==Kind::Color&&Inside(w,e.x,e.y)&&e.x<w.x+108){
    constexpr const char* colors[]={"#75E6C8","#AA9AFF","#FFCB78","#FF5A5F","#F2F0FF"};
    int n=std::clamp(static_cast<int>((e.x-w.x)/22),0,4);s.values[c.section][c.key]=colors[n];m_changed=true;
   }else if(c.kind==Kind::Toggle||Inside(w,e.x,e.y))Activate(i,s,now);
   return;
  }
 }
// 键盘事件：Tab 导航、Enter 激活、方向键调整、翻页。
 if(e.type==UiEventType::KeyDown){
  if(e.key==input::Key::Tab){int count=static_cast<int>(m_controls.size());m_focus=(m_focus+(e.shift?-1:1)+count+9)%(count+9);if(m_focus>=count)m_focus-=count+9;EnsureVisible();}
  else if(e.key==input::Key::Enter||e.key==input::Key::Space){if(m_focus==-1)ToggleEnabled();else if(m_focus<-1){m_page=-m_focus-2;m_controls=Controls(m_page);m_scroll=0;m_focus=m_controls.empty()?-1:0;m_hover=-1;m_popup=-1;}else Activate(m_focus,s,now);}
  else if(e.key==input::Key::Left||e.key==input::Key::Right){if(m_focus>=0&&m_controls[m_focus].kind==Kind::Combo)Activate(m_focus,s,now);else Adjust(m_focus,e.key==input::Key::Left?-1:1,s);}
  else if(e.key==input::Key::PageDown||e.key==input::Key::PageUp){m_scroll+=e.key==input::Key::PageDown?240:-240;Resize(m_width,m_height,m_scale);}
 }
}
// 绘制入口：组装 DrawList。
DrawList SettingsPanel::Paint(const SettingsModel& s,double now){
// 创建 Painter 并取主题色。
 Painter p(m_width,m_height);auto accent=Hex(s.values["general"]["accent"].get<std::string>());
 const Color white=Hex("#E8ECF1"),muted=Hex("#95A0B1");
 const bool chinese=s.values["general"]["language"]=="zh-CN";
 auto text=[&](std::string_view value,float x,float y,float size,Color color){p.Text(m_text.Shape(Translate(value,chinese),size,m_scale),{x,y},color);};
// 背景与侧边栏底色。
 p.FillRect({0,0,m_width,m_height},Hex("#11151C"));p.FillRect({0,0,160,m_height},Hex("#171B24"));
// 品牌 Logo 与应用名。
 PaintBrand(p,{12,17},28);text("InputHalo",46,22,17,white);
 text("MAKE INPUT VISIBLE",14,56,9,muted);
// 侧边栏八个页面按钮。
 for(int i=0;i<8;++i){if(i==m_page||m_focus==-i-2)p.RoundedRect({12,104+i*38.f,136,32},5,Hex(i==m_page?"#38334B":"#252C37"));text(Pages[i],24,113+i*38.f,12,i==m_page?Hex("#D2C5FF"):white);}
// 底部覆盖层启用开关。
 p.RoundedRect({12,m_height-60,136,32},5,Hex("#272E3A"));p.Circle({24,m_height-44},3,m_enabled?accent:muted);text(m_enabled?"Overlays on":"Overlays paused",34,m_height-51,11,white);
 text("InputHalo " IH_PRODUCT_VERSION,14,m_height-18,9,muted);
// 当前页标题与描述。
 text(Pages[m_page],176,17,22,white);text(Descriptions[m_page],176,48,11,muted);
// 按页面类型分发绘制：关于/方案/普通控件页。
 if(m_page==7){m_about.Paint(p,m_width,m_height,m_scale,chinese);}else if(m_page==5||m_page==6){m_library.Paint(p,m_width,m_height,m_scale,chinese,s,m_page==6);}else{
// 普通页(非首页)绘制实时预览卡片。
 if(m_page!=0){
  Rect preview{176,76,m_width-192,76};p.RoundedRect(preview,6,Hex("#1C2432"));p.PushClip(preview);
  text("LIVE PREVIEW",preview.x+12,preview.y+12,9,Hex("#C5B9FF"));text("Click to replay",preview.x+12,preview.y+34,10,muted);
  const Point center{preview.x+preview.width*.7f,preview.y+37};
// 计算预览脉冲进度。
  const float elapsed=static_cast<float>(now-m_previewStart),pulse=elapsed>=0&&elapsed<.7f&&!m_reduceMotion?elapsed/.7f:1.f;
// 光标预览：高亮 + 点击波纹。
  if(m_page==2){
   auto sample=s;sample.values["cursor"]["size"]=std::min(54.f,s.values["cursor"]["size"].get<float>());
   if(s.values["cursor"]["enabled"].get<bool>())features::OverlayScene::PaintCursor(p,center,sample);
   if(pulse<1&&s.values["cursor"]["click_ripple"].get<bool>())p.Ring(center,10+24*pulse,2,Hex(s.values["cursor"]["left_color"].get<std::string>(),1-pulse));
   p.Line({center.x-3,center.y-8},{center.x+3,center.y+8},2,white);p.Line({center.x-3,center.y-8},{center.x+11,center.y+3},2,white);
// 操作提示预览：键盘按键与鼠标示意。
  }else if(m_page==1){
   const char* keys[]={"Ctrl","Shift","K"};if(s.values["input"]["keyboard_enabled"].get<bool>())for(int i=0;i<3;++i){p.RoundedRect({center.x-86+i*46.f,center.y-13,40,26},4,Hex("#343D51"));text(keys[i],center.x-80+i*46.f,center.y-6,10,pulse<.65f?accent:white);}
   if(s.values["input"]["mouse_enabled"].get<bool>()){p.RoundedRect({center.x+70,center.y-20,26,38},9,Hex("#394458"));p.RoundedRect({center.x+73,center.y-17,9,13},3,accent);}
// 标注预览：两个矩形随脉冲生长。
  }else if(m_page==4){
   auto color=Hex(s.values["drawing"]["color"].get<std::string>());const float width=s.values["drawing"]["width"].get<float>();
   if(s.values["drawing"]["enabled"].get<bool>())for(int n=0;n<2;++n){float x=center.x-70+n*46.f,y=center.y-22+n*12.f;float w=58,h=32;if(n==1&&pulse<1){w*=pulse;h*=pulse;}features::OverlayScene::PaintAnnotation(p,{x,y},{x+w,y+h},s.values["drawing"]["shape"].get<std::string>(),width,color);}
// 激光预览：轨迹或指示点。
  }else{
   auto color=Hex(s.values["laser"]["color"].get<std::string>());const float stroke=std::min(8.f,s.values["laser"]["width"].get<float>());
   if(s.values["laser"]["enabled"].get<bool>()){
    if(s.values["laser"]["mode"]=="trail"){Point previous{center.x-76,center.y};for(int n=1;n<=static_cast<int>(32*pulse);++n){float t=n/32.f;Point next{center.x-76+t*152,center.y-std::sin(t*6)*15};auto core=color;core.a=.25f+.75f*t;p.Line(previous,next,stroke,core);previous=next;}}
    else{p.Glow(center,20,color);p.Circle(center,stroke,white);}
   }
  }p.PopClip();
 }
// 当前分区小标题。
 text(m_page==0?"Preferences":m_page==1?"Visibility & timing":m_page==2?"Appearance & response":m_page==3?"Beam & behavior":"Hold to draw · release to clear",176,ContentTop()-18,10,muted);
// 裁剪内容滚动区。
 p.PushClip({172,ContentTop()-1,m_width-180,std::max(0.f,m_height-ContentTop()-26)});
// 逐行绘制控件。
 for(int i=0;i<static_cast<int>(m_controls.size());++i){
  auto r=Row(i);if(r.y+r.height<ContentTop()||r.y>m_height-28)continue;auto w=Widget(i);auto& c=m_controls[i];auto& v=s.values[c.section][c.key];
  if(i==m_hover)p.RoundedRect(r,4,Hex("#1C2330"));
  p.Line({r.x,r.y+r.height},{r.x+r.width,r.y+r.height},1,Hex("#252C39"));
  p.PushClip({r.x,r.y,std::max(0.f,w.x-r.x-10),r.height});text(c.label,r.x+4,r.y+13,12,white);p.PopClip();
  if(i==m_focus){p.RoundedRect({w.x-2,w.y-2,w.width+4,w.height+4},5,Hex("#665D83"));p.RoundedRect(w,4,Hex("#202735"));}
// Toggle 开关绘制。
  if(c.kind==Kind::Toggle){p.RoundedRect({w.x,w.y+5,36,18},9,v.get<bool>()?accent:Hex("#454D5B"));p.Circle({w.x+(v.get<bool>()?27:9),w.y+14},6,v.get<bool>()?Hex("#172229"):white);}
// Slider 绘制：旋钮角度 + 轨道 + 数值文本。
  else if(c.kind==Kind::Slider){
   const double value=v.get<double>();const float t=static_cast<float>(std::clamp((value-c.low)/(c.high-c.low),0.,1.));const std::string_view key=c.key;
   const bool percent=key=="opacity"||key=="glow";
   std::string valueText=percent?std::format("{:.0f}%",value*100):key=="scale"?std::format("{:.2f}x",value):key.ends_with("ms")?std::format("{:.0f} ms",value):std::format("{:.1f} px",value);
   Point center{w.x+14,w.y+14};p.Ring(center,12,2,Hex("#495369"));p.Circle(center,9,Hex("#303A4D"));float angle=2.35619f+t*4.71239f;p.Line(center,{center.x+std::cos(angle)*9,center.y+std::sin(angle)*9},2,accent);
   const float length=w.width-106;p.Line({w.x+38,w.y+14},{w.x+38+length,w.y+14},2,Hex("#404C62"));p.Line({w.x+38,w.y+14},{w.x+38+length*t,w.y+14},2,accent);p.Circle({w.x+38+length*t,w.y+14},3,accent);
   text(valueText,w.x+w.width-62,w.y+8,11,white);
// Combo 分段按钮绘制。
  }else if(c.kind==Kind::Combo){
   const float segment=(w.width+4)/static_cast<float>(c.choices.size());
   for(int n=0;n<static_cast<int>(c.choices.size());++n){bool selected=v.get<std::string>()==c.choices[n];Rect tile{w.x+n*segment,w.y,segment-4,28};p.RoundedRect(tile,4,Hex(selected?"#48405F":"#283140"));text(std::string_view(c.key)=="language"?c.choices[n]:Display(c.choices[n]),tile.x+9,tile.y+8,11,selected?white:muted);}
// Color 绘制：色板 + 当前色块。
  }else if(c.kind==Kind::Color){
   constexpr const char* palette[]={"#75E6C8","#AA9AFF","#FFCB78","#FF5A5F","#F2F0FF"};for(int n=0;n<5;++n){Point point{w.x+10+n*22.f,w.y+14};p.RoundedRect({point.x-6,point.y-6,12,12},3,Hex(palette[n]));if(v.get<std::string>()==palette[n])p.Ring(point,10,1,white);}
   p.RoundedRect({w.x+116,w.y,w.width-116,28},4,Hex("#2C3545"));p.Circle({w.x+128,w.y+14},5,Hex(v.get<std::string>()));text(v.get<std::string>(),w.x+139,w.y+8,10,white);
// Hotkey 录制框绘制。
  }else{p.RoundedRect(w,4,Hex(m_recording?"#48405C":"#2C3545"));text(m_recording?"Press shortcut...":v.get<std::string>(),w.x+10,w.y+8,11,white);}
 }
 p.PopClip();
// 内容滚动条。
 const float visibleHeight=m_height-ContentTop()-28,totalHeight=ContentHeight();
 if(totalHeight>visibleHeight){float thumb=visibleHeight*visibleHeight/totalHeight;p.RoundedRect({m_width-7,ContentTop()+m_scroll*visibleHeight/totalHeight,2,thumb},1,muted);}
// 底部帮助/状态条。
 p.PushClip({176,m_height-24,m_width-192,22});text(m_hover>=0?m_controls[m_hover].help:status,176,m_height-17,9,muted);p.PopClip();
// 颜色弹出层绘制。
 if(m_popup>=0){auto& c=m_controls[m_popup];auto box=Popup();p.RoundedRect({box.x-1,box.y-1,box.width+2,box.height+2},7,Hex("#83729E"));p.RoundedRect(box,6,Hex("#242C3A"));
  auto color=Hex(s.values[c.section][c.key].get<std::string>());p.Circle({box.x+24,box.y+28},10,color);text(s.values[c.section][c.key].get<std::string>(),box.x+46,box.y+20,13,white);float rgb[]={color.r,color.g,color.b};const char* labels[]={"R","G","B"};
  for(int n=0;n<3;++n){text(labels[n],box.x+18,box.y+70+n*44.f,12,m_colorChannel==n?accent:white);p.Line({box.x+46,box.y+78+n*44.f},{box.x+270,box.y+78+n*44.f},3,muted);p.Circle({box.x+46+224*rgb[n],box.y+78+n*44.f},5,accent);}text("Tab: channel   Arrows: adjust   Enter: done",box.x+14,box.y+206,10,muted);
 }
// 普通控件内容结束。
 } // ordinary settings content
 (void)now;
 auto result=p.List();
// 计算标题栏高度并把内容命令整体下移标题栏高度。
 const float titleHeight=40*m_chromeScale;
 for(auto& command:result.commands){
     command.bounds.y+=titleHeight;
     if(command.parameters[0]==static_cast<float>(Shape::Line)){command.endpoints[1]+=titleHeight;command.endpoints[3]+=titleHeight;}
 }
 for(auto& batch:result.batches)batch.clip.y+=titleHeight;
// 单独创建标题栏 Painter。
 Painter chrome(m_width,m_height+titleHeight);
// 标题栏底色。
 chrome.FillRect({0,0,m_width,titleHeight},Hex("#101419"));
 const float u=m_chromeScale;
// 标题栏品牌与标题文字。
 PaintBrand(chrome,{10*u,8*u},24*u);
 chrome.Text(m_text.Shape("InputHalo",12*u,m_scale),{40*u,12*u},white);
 chrome.Text(m_text.Shape(Translate("Settings",chinese),11*u,m_scale),{108*u,13*u},muted);
// 绘制最小化/最大化/关闭三个窗口按钮。
 for(int button=0;button<3;++button){
     const float bx=m_width-(3-button)*46*u;
     if(m_chromeHover==button)chrome.FillRect({bx,0,46*u,titleHeight},Hex(button==2?"#C74452":"#28323D"));
     const float cx=bx+23*u,cy=20*u;
     if(button==0)chrome.Line({cx-5*u,cy},{cx+5*u,cy},u,white);
     else if(button==2){chrome.Line({cx-4*u,cy-4*u},{cx+4*u,cy+4*u},u,white);chrome.Line({cx+4*u,cy-4*u},{cx-4*u,cy+4*u},u,white);}
     else{
         auto outline=[&](float x,float y){chrome.Line({x,y},{x+9*u,y},u,white);chrome.Line({x+9*u,y},{x+9*u,y+9*u},u,white);chrome.Line({x+9*u,y+9*u},{x,y+9*u},u,white);chrome.Line({x,y+9*u},{x,y},u,white);};
         if(m_maximized)outline(cx-2*u,cy-6*u);
         outline(cx-5*u,cy-3*u);
     }
 }
// 标题栏与内容分隔线。
 chrome.Line({0,titleHeight-u},{m_width,titleHeight-u},u,Hex("#242B34"));
// 合并内容与标题栏命令，并同步图集版本。
 const auto base=static_cast<uint32_t>(result.commands.size());
 result.commands.insert(result.commands.end(),chrome.List().commands.begin(),chrome.List().commands.end());
 for(auto batch:chrome.List().batches){batch.first+=base;result.batches.push_back(batch);}
 if(chrome.List().atlas&&(!result.atlas||chrome.List().atlas->revision>result.atlas->revision))result.atlas=chrome.List().atlas;
 return result;
}
}




