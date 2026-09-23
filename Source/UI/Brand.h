/*******************************************************************************
 * 文件: Brand.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   内联函数 PaintBrand：用 2D Painter 原生矢量绘制 InputHalo 品牌图标。
 *   几何与配色与 Assets/Icons/InputHalo.svg 一致，不涉及复杂算法。
 *
 * 设计哲学:
 *   不加载位图图标，直接发出 Vulkan 矢量绘制命令，
 *   从而任意 DPI 下锐利、零纹理内存、与 UI 同一渲染管线。
 *
 * 技术特性:
 *   - 以 size/256 作为单位缩放系数
 *   - block lambda 用圆角矩形拼装各色块
 *
 * 依赖关系:
 *   内部: Render2D/Painter.h、Render2D/Color.h
 *
 * 注意事项:
 *   几何需与 SVG 资源保持同步；改动一边要改另一边。
 ******************************************************************************/
#pragma once
#include "Render2D/Painter.h"
#include "Render2D/Color.h"
// InputHalo UI 层命名空间。
namespace ih::ui {
// Same geometry and palette as Assets/Icons/InputHalo.svg, drawn natively by Vulkan.
// 原生绘制品牌 Logo：origin 为左上角，size 为目标边长。
inline void PaintBrand(render2d::Painter& painter,render2d::Point origin,float size){
// 以 256 设计网格为基准缩放。
 const float s=size/256;
// 圆角矩形绘制 lambda：按网格坐标与颜色画一块。
 auto block=[&](float x,float y,float w,float h,float radius,const char* color){
  painter.RoundedRect({origin.x+x*s,origin.y+y*s,w*s,h*s},radius*s,render2d::Hex(color));
 };
// 深色圆角底卡。
 block(8,8,240,240,56,"#171923");
// 两侧深色竖条(图标凹槽)。
 block(52,96,44,108,12,"#080B12");block(160,64,44,140,12,"#080B12");
// 中间紫色横条。
 block(80,120,96,32,10,"#AA9AFF");
// 前景高亮竖条。
 block(52,84,44,108,12,"#F2F0FF");block(160,52,44,140,12,"#AA9AFF");
// 顶部强调色圆点。
 block(108,52,28,28,8,"#FFCB78");
}
}
