/*******************************************************************************
 * 文件: SettingsPanel.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   声明主设置面板：编排侧边栏分页、控件行、弹出颜色/组合菜单、快捷键录制、
 *   实时预览与窗口标题栏(chrome)。不涉及复杂算法。
 *
 * 设计哲学:
 *   分页由 Controls(page) 元数据驱动渲染；面板聚合 AboutPanel 与 LibraryPanel 子面板；
 *   状态变更经 Changed() 一次性上报，避免高频轮询。
 *
 * 技术特性:
 *   - 行/控件/弹出层几何集中在 Row/Widget/Popup 计算
 *   - Slider 支持旋钮与线性轨道两种拖动手势
 *   - m_reduceMotion 可关闭动画
 *
 * 依赖关系:
 *   内部: UI/Strings.h、UI/AboutPanel.h、UI/LibraryPanel.h、Core/Events.h、Features/OverlayScene.h
 *
 * 注意事项:
 *   事件坐标已除以 m_scale；录制状态由本面板与 library 合并。
 ******************************************************************************/
#pragma once
#include "UI/Strings.h"
#include "UI/AboutPanel.h"
#include "UI/LibraryPanel.h"
#include "Core/Events.h"
#include "Features/OverlayScene.h"
// InputHalo UI 层命名空间。
namespace ih::ui {
// 主设置面板：侧边栏分页 + 元数据驱动控件 + 子面板聚合。
class SettingsPanel {
public:
// 构造：默认加载第 0 页控件，并构造关于/方案子面板。
 explicit SettingsPanel(text::TextEngine& text):m_text(text),m_controls(Controls(0)),m_about(text),m_library(text){}
// 调整窗口尺寸并据此收敛滚动。
 void Resize(float width,float height,float scale);
// 设置标题栏缩放/最大化/悬停状态。
 void Chrome(float scale,bool maximized,int hover){m_chromeScale=scale;m_maximized=maximized;m_chromeHover=hover;}
// 处理 UI 事件(now 用于动画时间)。
 void Event(UiEvent event,SettingsModel& settings,double now);
// 直通原始输入事件(快捷键录制)。
 void Raw(const input::Event& event,SettingsModel& settings);
// 绘制并返回命令列表 DrawList。
 render2d::DrawList Paint(const SettingsModel& settings,double now);
// 是否处于录制态(含子面板)。
 bool Recording()const{return m_recording||m_library.Recording();}
// 是否有未上报变更(合并本面板与 library)。
 bool Changed(){const bool library=m_library.Changed();return std::exchange(m_changed,false)||library;}
// 暴露方案子面板引用。
 LibraryPanel& Library(){return m_library;}
 void MarkChanged(){m_changed=true;}
// 覆盖层是否启用。
 bool Enabled()const{return m_enabled;}
// 切换覆盖层启用状态。
 void ToggleEnabled(){m_enabled=!m_enabled;m_changed=true;}
// 询问某时刻是否仍在播放进入动画。
 bool Animating(double now)const{return now<m_motionUntil;}
// 设置是否减弱动画。
 void ReduceMotion(bool value){m_reduceMotion=value;}
// 底部状态文案。
 std::string status="Changes save automatically";
private:
// 计算第 index 行的矩形。
 render2d::Rect Row(int index)const;
// 计算第 index 控件 widget 的矩形。
 render2d::Rect Widget(int index)const;
// 激活某控件(开关/弹出/录制)。
 void Activate(int index,SettingsModel& settings,double now);
// 调整滑杆(direction 为 ±步长)。
 void Adjust(int index,double direction,SettingsModel& settings);
// 拖动处理。
 void Drag(float x,float y,SettingsModel& settings);
// 内容总高度(用于滚动)。
 float ContentHeight()const;
// 内容顶部偏移(首页与其它页不同)。
 float ContentTop()const{return m_page==0?88.f:180.f;}
// 弹出菜单矩形。
 render2d::Rect Popup()const;
// 必要时滚动使焦点行可见。
 void EnsureVisible();
// 文本引擎引用。
 text::TextEngine& m_text;
// 当前页控件元数据。
 std::vector<Control> m_controls;
// 窗口尺寸/缩放/滚动。
 float m_width=960,m_height=640,m_scale=1,m_scroll=0;
// 页面/焦点/悬停/拖动/弹出/选项/颜色通道等交互状态。
 int m_page=0,m_focus=-1,m_hover=-1,m_drag=-1,m_popup=-1,m_choice=0,m_colorChannel=-1;
// 录制/变更/启用/减弱动画标志。
 bool m_recording=false,m_changed=false,m_enabled=true,m_reduceMotion=false;
// 进入动画截止时间。
 double m_motionUntil=0;
// 快捷键录制的按键状态。
 input::State m_recordState;
// 标题栏外观状态。
 float m_chromeScale=1;
 bool m_maximized=false;
 int m_chromeHover=-1;
// 拖动起始 Y 与起始值。
 float m_dragY=0;double m_dragValue=0;
// 实时预览动画起始时间。
 double m_previewStart=-10;
// 是否正在拖动颜色通道。
 bool m_colorDragging=false;
// 是否线性轨道拖动。
 bool m_linearDrag=false;
// 当前录制目标控件索引。
 int m_recordTarget=-1;
// 关于子面板。
 AboutPanel m_about;
// 方案库子面板。
 LibraryPanel m_library;
};
}
