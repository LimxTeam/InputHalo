/*******************************************************************************
 * 文件: DrawList.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   Render2D 层核心数据结构：几何/颜色值类型、图元类型枚举、64 字节绘制命令、批次与绘制列表。
 *   不涉及复杂算法；命令按绘制顺序追加，按裁剪区间合并为批次。
 *   用紧凑的 64 字节 Command（4 个 float4）换取与 GPU storage buffer / HLSL 的零拷贝布局。
 *
 * 设计哲学:
 *   将"绘制什么"（Command）与"按什么裁剪顺序画"（Batch）解耦：Painter 只产生命令流，
 *   RHI 层直接把命令数组上传为 storage buffer，由顶点着色器按实例索引解释形状类型。
 *   几何使用 DIP（设备无关像素）、颜色使用线性 sRGB，与 GPU 端约定保持一致。
 *
 * 技术特性:
 *   - 固定布局: static_assert(sizeof(Command)==64)，与着色器端 Cbuffer 对齐。
 *   - 批次裁剪: 相邻且裁剪矩形相同的命令合并为同一 Batch，一次 scissor 绘制。
 *   - 形状多态: 用 parameters[0] 编码 Shape 枚举，单个管线绘制所有图元。
 *   - 图集共享: DrawList 持有 text::Atlas 弱引用，文字图元通过 endpoints 携带 UV。
 *
 * 依赖关系:
 *   内部: Limx::ih::render2d、Limx::text::Atlas
 *   外部: C++23 <array>/<cstdint>/<vector>，布局需与 HLSL/SPIR-V 对齐
 *
 * 注意事项:
 *   禁止在一个 DrawList 中混用多个文字引擎（由 Painter 校验）。
 *   MaxCommands=4096 为单帧命令上限，超出会抛 length_error。
 *
 ******************************************************************************/
#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include "Text/FontAtlas.h"

namespace ih::render2d {
// 二维点（DIP 坐标），用于圆心、起点终点等。
struct Point { float x{}, y{}; };
// 轴对齐矩形：左上角 + 宽高，是绝大多数图元的包围盒。
struct Rect { float x{}, y{}, width{}, height{}; };
// 归一化 RGBA 颜色，默认 alpha=1（不透明）；直出 sRGB。
struct Color { float r{}, g{}, b{}, a{1}; };
// 图元类型枚举，编码进 Command.parameters[0]，顶点着色器据此分支。
enum class Shape : uint32_t { Rectangle, RoundedRectangle, Circle, Line, Glyph, Ring, Glow };
// Four float4s, shared with HLSL. Geometry is in DIP; color is straight sRGB.
// 绘制命令：4 个 float4（bounds/color/parameters/endpoints），与 HLSL 共享内存布局。
// geometry 单位为 DIP，颜色为直出 sRGB；见上方原注释。
struct Command {
    Rect bounds;
    Color color;
    std::array<float, 4> parameters{}; // type, radius/half stroke width, atlas page, reserved
    std::array<float, 4> endpoints{}; // line endpoints or glyph UV rectangle
};
// 编译期断言 Command 恰为 64 字节，保证与 GPU 端布局一致。
static_assert(sizeof(Command) == 64);
// 批次：指向命令数组区间 [first, first+count)，并记录该区间的裁剪矩形。
struct Batch { uint32_t first{}, count{}; Rect clip; };
// 绘制列表：命令流 + 批次索引 + 共享字体图集，是 Render2D 层的最终产物。
struct DrawList {
    std::vector<Command> commands;
    std::vector<Batch> batches;
    std::shared_ptr<const text::Atlas> atlas;
};
// 单帧命令容量上限（4096），Painter 与 ShapeBuffer 共同遵守。
inline constexpr uint32_t MaxCommands = 4096;
}
