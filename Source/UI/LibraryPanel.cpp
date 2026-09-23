/*******************************************************************************
 * 文件: LibraryPanel.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   实现方案/排除面板的命令分发、UI 事件处理、快捷键录制与绘制。
 *   Run 把按钮命令映射为动作或列表增删；Raw 在录制态捕获非修饰键组合并入排除热键；
 *   Paint 重建按钮并绘制可裁剪列表与状态行。
 *   复杂度: O(列表项数) 的线性命中测试与绘制。
 *
 * 设计哲学:
 *   按钮在 Paint 阶段重建并记录命中矩形，Event 命中测试复用同一坐标，
 *   保证绘制与交互永远一致；删除需二次确认以防误删。
 *
 * 技术特性:
 *   - 滚轮/PageUpDn 滚动，Tab 焦点循环
 *   - 录制态忽略单独的修饰键，组合经 Modifiers() 合成
 *   - 排除项上限 128 并去重
 *
 * 依赖关系:
 *   内部: UI/Localization.h、Render2D/Color.h
 *   外部: render2d、input
 *
 * 注意事项:
 *   录制中收到 Escape 或 Reset 即取消。
 ******************************************************************************/
#include "UI/LibraryPanel.h"
#include "UI/Localization.h"
#include "Render2D/Color.h"
#include <algorithm>
// InputHalo UI 层命名空间实现。
namespace ih::ui {
using namespace render2d;
// 命令分发：根据按钮编号执行对应逻辑。
void LibraryPanel::Run(int command,SettingsModel& settings){
// 编号 >=1000：删除排除列表中的某条目。
    if(command>=1000){
        const size_t index=static_cast<size_t>(command-1000);auto& list=settings.values["exclusions"][m_tab==0?"apps":"hotkeys"];
        if(index<list.size()){list.erase(index);m_changed=true;}return;
    }
// 编号 >=100：选中某个方案条目。
    if(command>=100){m_selected=command-100;m_confirmDelete=false;return;}
// 编号 10/11：切换“应用/快捷键”排除 tab。
    if(command==10||command==11){m_tab=command-10;m_scroll=0;m_focus=-1;return;}
// 编号 12：请求添加应用。
    if(command==12){m_action=LibraryAction::AddApplication;return;}
// 编号 13：进入快捷键录制态。
    if(command==13){m_recording=true;m_recordState.Reset();status="Press the shortcut to hide; Escape cancels";return;}
// 编号 1~5：保存/应用/删除/导入/导出方案(删除需二次确认)。
    if(command==1)m_action=LibraryAction::SaveProfile;
    if(command==2)m_action=LibraryAction::ApplyProfile;
    if(command==3){if(!m_confirmDelete){m_confirmDelete=true;return;}m_action=LibraryAction::DeleteProfile;m_confirmDelete=false;}
    if(command==4)m_action=LibraryAction::ImportProfile;
    if(command==5)m_action=LibraryAction::ExportProfile;
}
// UI 事件处理。
void LibraryPanel::Event(const UiEvent& e,SettingsModel& settings,bool){
// 失焦时取消所有临时状态。
    if(e.type==UiEventType::Blur){Cancel();return;}
// 录制态不响应普通 UI 事件。
    if(m_recording)return;
// 滚轮滚动。
    if(e.type==UiEventType::Wheel){m_scroll=std::clamp(m_scroll-e.wheel*48,0.f,m_maxScroll);return;}
// 按下：遍历按钮做命中测试。
    if(e.type==UiEventType::Down){
// 命中可用按钮则记录焦点并执行其命令。
        for(size_t i=0;i<m_buttons.size();++i){const auto& b=m_buttons[i];const auto r=b.bounds;if(b.enabled&&e.x>=r.x&&e.x<r.x+r.width&&e.y>=r.y&&e.y<r.y+r.height){m_focus=static_cast<int>(i);Run(b.command,settings);return;}}
        m_confirmDelete=false;
    }
// 键盘事件。
    if(e.type==UiEventType::KeyDown){
// Escape 取消。
        if(e.key==input::Key::Escape){Cancel();return;}
// Tab/Shift+Tab 循环移动焦点。
        if(e.key==input::Key::Tab&&!m_buttons.empty())m_focus=(m_focus+(e.shift?-1:1)+static_cast<int>(m_buttons.size()))%static_cast<int>(m_buttons.size());
// Enter/Space 激活焦点按钮。
        if((e.key==input::Key::Enter||e.key==input::Key::Space)&&m_focus>=0&&m_focus<static_cast<int>(m_buttons.size())&&m_buttons[m_focus].enabled)Run(m_buttons[m_focus].command,settings);
        if(e.key==input::Key::PageDown||e.key==input::Key::PageUp)m_scroll=std::clamp(m_scroll+(e.key==input::Key::PageDown?200:-200),0.f,m_maxScroll);
    }
}
// 原始输入事件：仅在录制态处理。
void LibraryPanel::Raw(const input::Event& e,SettingsModel& settings){
// 把事件并入按键状态。
    if(!m_recording)return;m_recordState.Apply(e,false);
// Reset 或 Escape 取消录制。
    if(e.type==input::EventType::Reset||(e.type==input::EventType::Down&&e.key==input::Key::Escape)){m_recording=false;status="Shortcut recording cancelled";return;}
// 只接受非修饰主键的按下。
    if(e.type!=input::EventType::Down||input::IsModifier(e.key)||e.key>=input::Key::MouseLeft)return;
// 合成带修饰键的热键组合。
    const auto key=m_recordState.Modifiers(e.key);if(!key.Valid())return;
// 规范化为热键名并加入排除列表。
    const auto name=input::FormatHotkey(key);auto& keys=settings.values["exclusions"]["hotkeys"];
// 去重且不超过 128 个。
    if(std::find(keys.begin(),keys.end(),name)==keys.end()&&keys.size()<128){keys.push_back(name);m_changed=true;}
    m_recording=false;status="Shortcut hidden; its original action still works";
}
// 绘制入口。
void LibraryPanel::Paint(Painter& p,float width,float height,float dpi,bool chinese,const SettingsModel& settings,bool exclusions){
    m_buttons.clear();const auto white=Hex("#E8ECF1"),muted=Hex("#95A0B1");
// 文本绘制 lambda(自动翻译)。
    auto text=[&](std::string_view value,float x,float y,float size=11){p.Text(m_text.Shape(Translate(value,chinese),size,dpi),{x,y},white);};
// 按钮绘制 lambda：重建并记录命中矩形。
    auto button=[&](Rect r,std::string label,int command,bool enabled=true,bool selected=false){
        const bool focused=m_focus==static_cast<int>(m_buttons.size());m_buttons.push_back({r,label,command,enabled});
        p.RoundedRect(r,4,Hex(selected?"#48405F":focused?"#3A4355":"#283140"));p.PushClip(r);p.Text(m_text.Shape(Translate(label,chinese),11,dpi),{r.x+8,r.y+8},enabled?white:muted);p.PopClip();
    };
// 计算列表起始位置。
    float top=156;size_t count{};
// 排除规则页：绘制 tab 与“添加/录制”按钮。
    if(exclusions){
        button({176,78,142,28},"Applications",10,true,m_tab==0);button({324,78,160,28},"Hidden shortcuts",11,true,m_tab==1);
        button({176,116,170,28},m_tab==0?"Add application...":m_recording?"Press shortcut...":"Record shortcut",m_tab==0?12:13,settings.values["exclusions"][m_tab==0?"apps":"hotkeys"].size()<128);
        count=settings.values["exclusions"][m_tab==0?"apps":"hotkeys"].size();
// 方案库页：绘制五个操作按钮。
    }else{
        const bool selected=m_selected>=0&&m_selected<static_cast<int>(profiles.size());
        const float segment=(width-192)/5;
        const char* names[]={"Save current...","Apply","Delete","Import...","Export..."};
        for(int i=0;i<5;++i)button({176+i*segment,78,segment-5,28},i==2&&m_confirmDelete?"Confirm delete":names[i],i+1,i==0||i==3||(selected&&(i!=2||!profiles[m_selected].builtin)));
        text("Presets keep your language, startup and exclusions",180,120,10);count=profiles.size();
    }
// 计算可视高度与滚动范围。
    const float available=std::clamp(height-top-76,40.f,450.f);m_maxScroll=std::max(0.f,count*38.f-available);m_scroll=std::clamp(m_scroll,0.f,m_maxScroll);
// 裁剪列表视口。
    p.PushClip({176,top,width-192,available});
// 逐行绘制列表项，跳过视口外。
    for(size_t i=0;i<count;++i){const float y=top+i*38.f-m_scroll;if(y<top||y+32>top+available)continue;
// 排除页：显示条目与“移除”按钮。
        if(exclusions){
            const auto label=settings.values["exclusions"][m_tab==0?"apps":"hotkeys"][i].get<std::string>();
            p.PushClip({180,y,width-290,32});text(label,180,y+8);p.PopClip();button({width-98,y,78,28},"Remove",1000+static_cast<int>(i));
// 方案页：整行作为可点选按钮。
        }else button({176,y,width-192,32},profiles[i].name,100+static_cast<int>(i),true,m_selected==static_cast<int>(i));
    }
// 空列表提示。
    if(!count)text("Nothing excluded",180,top+10);
    p.PopClip();
// 滚动条。
    if(m_maxScroll>0){const float thumb=available*available/(count*38.f);p.RoundedRect({width-9,top+m_scroll*available/(count*38.f),2,thumb},1,muted);}
// 底部帮助与状态行。
    p.PushClip({176,height-68,width-192,66});
    if(exclusions){text(m_tab==0?"Matching foreground apps pause all overlays":"Only hides exact key combinations; does not block keys",180,height-62,10);}
    text(status,180,height-26,10);p.PopClip();
}
}
