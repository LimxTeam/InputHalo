/*******************************************************************************
 * 文件: Painter.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   2D 画笔 Painter 的声明：对外提供矩形/圆角/圆/环/辉光/线/文字等绘制接口与裁剪栈控制。
 *   不涉及复杂算法；仅声明接口，实现在 Painter.cpp。
 *   无额外空间权衡：持有一个 DrawList 与一个裁剪栈向量。
 *
 * 设计哲学:
 *   Painter 是 Render2D 层唯一的绘制入口，刻意不依赖任何平台或 GPU 类型，
 *   使录制逻辑可在无图形环境下单测；绘制顺序在跨批次时仍被严格保留。
 *
 * 技术特性:
 *   - 平台无关: 头文件不引入任何 Vulkan/Win32 类型。
 *   - RAII 式裁剪: PushClip/PopClip 配对管理裁剪栈。
 *   - 只读产物: List() 返回 const DrawList，供 RHI 层上传。
 *
 * 依赖关系:
 *   内部: Limx::ih::render2d::DrawList
 *   外部: 仅 <vector>（经 DrawList.h 传递）
 *
 * 注意事项:
 *   使用者需在 Begin/End 之间录制，最终通过 List() 取走命令流。
 *   不允许在 DrawList 录制完成后继续修改它。
 *
 ******************************************************************************/
#pragma once
#include "Render2D/DrawList.h"

namespace ih::render2d {
// No platform or GPU dependencies. Painter order is preserved across clip batches.
// Painter: 2D 绘制录制器（无平台/GPU 依赖）；绘制顺序在跨裁剪批次时仍被保留。
class Painter {
public:
    Painter(float width, float height);
    void FillRect(Rect bounds, Color color);
    void RoundedRect(Rect bounds, float radius, Color color);
    void Circle(Point center, float radius, Color color);
    void Ring(Point center, float radius, float width, Color color);
    void Glow(Point center, float radius, Color color);
    void Line(Point start, Point end, float width, Color color);
    void Text(const text::Layout& layout, Point origin, Color color);
    void PushClip(Rect clip);
    void PopClip();
// List: 取走录制完成的只读绘制列表，交给 RHI 层渲染。
    const DrawList& List() const { return m_list; }
private:
// Add: 内部统一入口，所有图元都经该校验/裁剪/批次逻辑写入命令流。
    void Add(Command command);
// m_list: 累积的命令与批次（绘制产物）。
    DrawList m_list;
// m_clips: 裁剪矩形栈，栈顶为当前生效裁剪，首元素为根裁剪。
    std::vector<Rect> m_clips;
};
}
