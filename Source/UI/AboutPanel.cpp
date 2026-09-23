/*******************************************************************************
 * 文件: AboutPanel.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   实现“关于”面板的事件处理、UTF-8 边界软换行排版与绘制。
 *   Event 处理 tab 点击/滚轮/方向键翻页；Layout 把多语言长文档软换行到固定列宽；
 *   Paint 绘制 tab 条、裁剪文本视口与滚动条。
 *   换行复杂度 O(行数 × 测量次数)。
 *
 * 设计哲学:
 *   换行在 UTF-8 字符边界断开并优先回退到空格；原始法律文本随可执行文件逐字节保留，
 *   这里仅做显示层包装，不改动法律文本本身。
 *
 * 技术特性:
 *   - 逐行用 TextEngine.Shape 测量宽度决定断点
 *   - 裁剪视口外的行直接跳过不画
 *   - 滚动条 thumb 按可见比计算
 *
 * 依赖关系:
 *   内部: UI/Brand.h、UI/Localization.h、ProductInfo.h、LegalText.h
 *   外部: render2d
 *
 * 注意事项:
 *   tab 共 4 项；行高 18px；底部提示条。
 ******************************************************************************/
#include "UI/AboutPanel.h"
#include "UI/Brand.h"
#include "UI/Localization.h"
#include "ProductInfo.h"
#include "LegalText.h"
#include <algorithm>
#include <sstream>
// InputHalo UI 层命名空间实现。
namespace ih::ui {
using namespace render2d;
// 事件处理：tab 点击、滚轮、方向键翻页，最后钳制滚动范围。
void AboutPanel::Event(const UiEvent& e){
// 命中 tab 条区域则切换 tab。
 if(e.type==UiEventType::Down&&e.x>=176&&e.x<m_width-16&&e.y>=76&&e.y<106){
// 按横向位置换算 tab 索引并复位滚动。
  m_tab=std::clamp(static_cast<int>((e.x-176)/((m_width-192)/4)),0,3);m_scroll=0;
 }
// 滚轮滚动。
 if(e.type==UiEventType::Wheel)m_scroll-=e.wheel*54;
// 键盘事件。
 if(e.type==UiEventType::KeyDown){
// 左右方向键循环切换四栏。
  if(e.key==input::Key::Left||e.key==input::Key::Right){m_tab=(m_tab+(e.key==input::Key::Left?3:1))%4;m_scroll=0;}
// 上下键微调滚动。
  if(e.key==input::Key::Down)m_scroll+=18;
  if(e.key==input::Key::Up)m_scroll-=18;
// PageUp/Down 按视口 85% 翻页。
  if(e.key==input::Key::PageDown)m_scroll+=m_viewHeight*.85f;
  if(e.key==input::Key::PageUp)m_scroll-=m_viewHeight*.85f;
// Home/End 跳到首/末。
  if(e.key==input::Key::Home)m_scroll=0;
  if(e.key==input::Key::End)m_scroll=static_cast<float>(m_lines.size())*18;
 }
// 把滚动钳到合法范围。
 m_scroll=std::clamp(m_scroll,0.f,std::max(0.f,static_cast<float>(m_lines.size())*18-m_viewHeight));
}
// 排版：tab/宽度/DPI/语言都没变则直接复用缓存。
void AboutPanel::Layout(float width,float dpi,bool chinese){
 if(m_cachedTab==m_tab&&m_width==width&&m_dpi==dpi&&m_chinese==chinese)return;
 m_width=width;m_dpi=dpi;m_chinese=chinese;m_cachedTab=m_tab;m_lines.clear();
// 中文概述文案(产品版本、版权、开源依赖列表)。
 const std::string overview=chinese?
  "InputHalo " IH_PRODUCT_VERSION " Preview\n\n开发团队：LimxTeam\n权利主体：" IH_COMPANY_ZH "\n" IH_COMPANY_EN "\n\nCopyright (c) 2026 " IH_COMPANY_ZH "\n保留所有权利。专有商业软件。\n\nInputHalo 将键盘、鼠标和演示标注清晰呈现在屏幕上。\n当前构建：Windows x64 / Vulkan；本地运行，无需管理员权限。\n\n开源依赖\nspdlog 1.15.3 — MIT\nfmt 11.2.0（spdlog 内置）— MIT\nnlohmann/json 3.12.0 — MIT\nFreeType 2.14.3 — FreeType License\nHarfBuzz 14.2.0 — Old MIT\nVolk（Vulkan SDK 1.4.357.0 内置）— MIT\nVulkan-Headers 1.4.357 — MIT（采用双许可证中的 MIT）\n\n第三方组件继续适用其各自许可证。完整原文见“开源声明”。\nWindows 字体由系统提供，不随本软件分发。\n\nPortions of this software are copyright © 2026 The FreeType Project (https://freetype.org). All rights reserved."
  :"InputHalo " IH_PRODUCT_VERSION " Preview\n\nDeveloped by LimxTeam\n" IH_COMPANY_EN "\n" IH_COMPANY_ZH "\n\nCopyright (c) 2026 " IH_COMPANY_EN "\nAll rights reserved. Proprietary commercial software.\n\nKeyboard, mouse and presentation annotations, made visible.\nWindows x64 / Vulkan. Local operation; no administrator rights required.\n\nOpen-source dependencies\nspdlog 1.15.3 — MIT\nfmt 11.2.0 (bundled with spdlog) — MIT\nnlohmann/json 3.12.0 — MIT\nFreeType 2.14.3 — FreeType License\nHarfBuzz 14.2.0 — Old MIT\nVolk (Vulkan SDK 1.4.357.0) — MIT\nVulkan-Headers 1.4.357 — MIT option of its dual license\n\nThird-party components retain their own license terms.\nFull original texts are available in Open-source notices.\nWindows fonts are supplied by the system, not bundled with InputHalo.\n\nPortions of this software are copyright © 2026 The FreeType Project (https://freetype.org). All rights reserved.";
// 按 tab 选择文档：0 概述，1 软件许可，2 开源声明，3 隐私。
 std::string_view document=m_tab==0?std::string_view(overview):m_tab==1?product::SoftwareLicense:m_tab==2?product::ThirdPartyNotices:product::PrivacyNotice;
 std::istringstream paragraphs{std::string(document)};std::string paragraph;
// 换行列宽(随面板宽度收敛在 200~640)。
 const float limit=std::clamp(width-216,200.f,640.f);
 // Wrap on UTF-8 boundaries; original notice files remain byte-for-byte available beside the executable.
// 逐段软换行。
 while(std::getline(paragraphs,paragraph)){
// 去掉行尾回车。
  if(!paragraph.empty()&&paragraph.back()=='\r')paragraph.pop_back();
// 空行保留为间距行。
  if(paragraph.empty()){m_lines.emplace_back();continue;}
  size_t begin=0;
// 在段落内按测量宽度断行。
  while(begin<paragraph.size()){
   size_t end=begin,lastSpace=begin,accepted=begin;
   while(end<paragraph.size()){
// 按 UTF-8 字符步进(避免切断多字节字符)。
    unsigned char ch=static_cast<unsigned char>(paragraph[end]);size_t bytes=ch<0x80?1:(ch<0xE0?2:ch<0xF0?3:4);
    end=std::min(end+bytes,paragraph.size());
// 测量当前片段宽度，超限则断行。
    if(m_text.Shape(std::string_view(paragraph).substr(begin,end-begin),11,dpi).width>limit&&accepted>begin)break;
    accepted=end;if(ch==' ')lastSpace=end;
   }
// 优先在空格处断行，避免把词劈成两半。
   if(accepted<paragraph.size()&&lastSpace>begin)accepted=lastSpace;
   if(accepted==begin)accepted=end;
// 写入一行并推进。
   m_lines.push_back(paragraph.substr(begin,accepted-begin));begin=accepted;
  }
 }
}
// 绘制入口：先排版并计算视口高度。
void AboutPanel::Paint(Painter& painter,float width,float height,float dpi,bool chinese){
 Layout(width,dpi,chinese);m_viewHeight=std::clamp(height-142,0.f,450.f);m_scroll=std::clamp(m_scroll,0.f,std::max(0.f,static_cast<float>(m_lines.size())*18-m_viewHeight));
// 四个 tab 标签。
 const char* tabs[]={"Product","Software license","Open-source notices","Privacy"};
 const float part=(width-192)/4;
// 绘制 tab 按钮并裁剪文字。
 for(int i=0;i<4;++i){Rect r{176+i*part,76,part-4,30};painter.RoundedRect(r,4,Hex(i==m_tab?"#48405F":"#283140"));painter.PushClip(r);painter.Text(m_text.Shape(Translate(tabs[i],chinese),11,dpi),{r.x+8,r.y+9},Hex("#E8ECF1"));painter.PopClip();}
// 裁剪文本视口。
 painter.PushClip({176,114,width-192,m_viewHeight});
// 逐行绘制，跳过完全在视口外的行。
 for(size_t i=0;i<m_lines.size();++i){float y=114+static_cast<float>(i)*18-m_scroll;if(y+18<114||y>114+m_viewHeight)continue;
  painter.Text(m_text.Shape(m_lines[i],11,dpi),{180,y},Hex(m_tab==0&&i==0?"#D2C5FF":"#C6CEDB"));
 }
 painter.PopClip();
// 内容超出视口时绘制滚动条。
 const float total=static_cast<float>(m_lines.size())*18;
 if(total>m_viewHeight){float thumb=m_viewHeight*m_viewHeight/total;painter.RoundedRect({width-8,114+m_scroll*m_viewHeight/total,2,thumb},1,Hex("#95A0B1"));}
// 底部操作提示。
 painter.Text(m_text.Shape(Translate("Scroll to read · Left / Right to switch",chinese),9,dpi),{176,height-17},Hex("#95A0B1"));
}
}
