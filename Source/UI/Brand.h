#pragma once
#include "Render2D/Painter.h"
#include "Render2D/Color.h"
namespace ih::ui {
// Same geometry and palette as Assets/Icons/InputHalo.svg, drawn natively by Vulkan.
inline void PaintBrand(render2d::Painter& painter,render2d::Point origin,float size){
 const float s=size/256;
 auto block=[&](float x,float y,float w,float h,float radius,const char* color){
  painter.RoundedRect({origin.x+x*s,origin.y+y*s,w*s,h*s},radius*s,render2d::Hex(color));
 };
 block(8,8,240,240,56,"#171923");
 block(52,96,44,108,12,"#080B12");block(160,64,44,140,12,"#080B12");
 block(80,120,96,32,10,"#AA9AFF");
 block(52,84,44,108,12,"#F2F0FF");block(160,52,44,140,12,"#AA9AFF");
 block(108,52,28,28,8,"#FFCB78");
}
}
