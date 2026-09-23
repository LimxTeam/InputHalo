/*******************************************************************************
 * 文件: LibraryPanel.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   声明方案库面板与排除规则面板：枚举内置/用户方案及应用、快捷键排除项，
 *   并向外发出高层动作请求(LibraryAction)。不涉及复杂算法。
 *
 * 设计哲学:
 *   面板只产生动作与状态标记，真正的文件读写由上层执行，
 *   保持面板无副作用、易于测试；命令用编号约定区分。
 *
 * 技术特性:
 *   - 命令编号约定：10/11 切 tab，1~5 方案操作，12/13 排除操作，100+ 选方案，1000+ 删条目
 *   - Recording/Changed/TakeAction 用 exchange 一次性取走状态
 *
 * 依赖关系:
 *   内部: Core/Events.h、Settings/ProfileStore.h、Input/InputState.h、Text/TextEngine.h、Render2D/Painter.h
 *
 * 注意事项:
 *   快捷键录制通过 Raw() 直通原始 input 事件。
 ******************************************************************************/
#pragma once
#include "Core/Events.h"
#include "Settings/ProfileStore.h"
#include "Input/InputState.h"
#include "Text/TextEngine.h"
#include "Render2D/Painter.h"
#include <utility>
// InputHalo UI 层命名空间。
namespace ih::ui {
// 面板向上层请求的动作枚举(由真正的业务层执行文件读写)。
enum class LibraryAction {None,SaveProfile,ApplyProfile,DeleteProfile,ImportProfile,ExportProfile,AddApplication};
// 方案库 / 排除规则面板。
class LibraryPanel {
public:
// 构造：注入文本引擎。
    explicit LibraryPanel(text::TextEngine& text):m_text(text){}
// 当前方案列表(含内置)。
    std::vector<Profile> profiles;
// 底部状态提示文案。
    std::string status="Select a preset to apply or export";
// 处理 UI 事件；exclusions 指示当前是否为排除规则页。
    void Event(const UiEvent& event,SettingsModel& settings,bool exclusions);
// 直通原始输入事件(用于快捷键录制)。
    void Raw(const input::Event& event,SettingsModel& settings);
// 绘制面板。
    void Paint(render2d::Painter& painter,float width,float height,float dpi,bool chinese,const SettingsModel& settings,bool exclusions);
// 取消录制/删除确认/焦点。
    void Cancel(){m_recording=false;m_confirmDelete=false;m_focus=-1;}
    bool Recording()const{return m_recording;}
// 一次性取走并清除“已变更”标记。
    bool Changed(){return std::exchange(m_changed,false);}
// 一次性取走并清除待执行动作。
    LibraryAction TakeAction(){return std::exchange(m_action,LibraryAction::None);}
    int Selected()const{return m_selected;}
    void Select(int value){m_selected=value;}
private:
// 按钮：命中矩形、标签、命令编号、是否可用。
    struct Button {render2d::Rect bounds;std::string label;int command;bool enabled;};
// 命令分发：把按钮命令映射为动作或列表增删。
    void Run(int command,SettingsModel& settings);
// 文本引擎引用。
    text::TextEngine& m_text;
// Paint 阶段重建的按钮列表(供 Event 命中测试复用)。
    std::vector<Button> m_buttons;
// 快捷键录制时的按键状态。
    input::State m_recordState;
// 待执行动作。
    LibraryAction m_action=LibraryAction::None;
// 选中方案、排除 tab、焦点按钮。
    int m_selected=0,m_tab=0,m_focus=-1;
// 滚动偏移与最大滚动。
    float m_scroll=0,m_maxScroll=0;
// 录制中/已变更/删除二次确认标志。
    bool m_recording=false,m_changed=false,m_confirmDelete=false;
};
}
