/*******************************************************************************
 * 文件: AboutPanel.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   声明“关于”面板：展示产品信息、软件许可、开源声明、隐私四栏文档，支持滚动与左右切换。
 *   不涉及复杂算法。
 *
 * 设计哲学:
 *   文档按 tab 缓存换行结果，仅在 tab/宽度/DPI/语言变化时重排，
 *   滚动与绘制零重排开销；面板本身不修改任何业务设置。
 *
 * 技术特性:
 *   - 持有 TextEngine 引用用于排版测量
 *   - m_lines 为软换行后的行序列
 *   - m_scroll 驱动视口偏移
 *
 * 依赖关系:
 *   内部: Core/Events.h、Text/TextEngine.h、Render2D/Painter.h
 *
 * 注意事项:
 *   纯展示型 UI，无副作用。
 ******************************************************************************/
#pragma once
#include "Core/Events.h"
#include "Text/TextEngine.h"
#include "Render2D/Painter.h"
#include <string>
#include <vector>
// InputHalo UI 层命名空间。
namespace ih::ui {
// “关于”面板：四栏法律/产品文档的可滚动展示。
class AboutPanel {
public:
// 构造：注入文本引擎。
 explicit AboutPanel(text::TextEngine& text):m_text(text){}
// 处理 UI 事件(tab 切换/滚动/方向键)。
 void Event(const UiEvent& event);
// 绘制面板。
 void Paint(render2d::Painter& painter,float width,float height,float dpi,bool chinese);
private:
// 按宽度/DPI/语言做软换行排版(带缓存)。
 void Layout(float width,float dpi,bool chinese);
// 文本引擎引用。
 text::TextEngine& m_text;
// 换行后的文档行序列。
 std::vector<std::string> m_lines;
// 当前 tab 与上次排版的 tab(缓存失效判定)。
 int m_tab=0,m_cachedTab=-1;
// 滚动偏移与布局缓存值。
 float m_scroll=0,m_width=0,m_dpi=0,m_viewHeight=0;
// 当前是否中文。
 bool m_chinese=false;
};
}
