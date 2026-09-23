/*******************************************************************************
 * 文件: Color.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   十六进制颜色字符串的解析工具，将 "#RRGGBB" 形式的颜色转换为归一化 RGBA 颜色。
 *   不涉及复杂算法；解析为 O(1) 的定长整数位运算，无堆分配。
 *   无额外空间开销，每次调用仅产生一个 Color 值。
 *
 * 设计哲学:
 *   作为 Render2D 层的纯函数内联工具，与平台/GPU 完全解耦，
 *   放在头文件中以便各处零成本内联；解析失败时静默退化为黑色，避免在绘制热路径抛异常。
 *
 * 技术特性:
 *   - 内联实现: 定义在头文件中，调用点直接展开。
 *   - 无异常热路径: 使用 std::from_chars，不抛异常、不分配。
 *   - 可定制透明度: alpha 作为独立参数，默认不透明 1.0。
 *
 * 依赖关系:
 *   内部: Limx::ih::render2d（Color 定义于 DrawList.h）
 *   外部: C++23 <string_view> / <charconv>
 *
 * 注意事项:
 *   仅识别 7 字符（含 '#'）的十六进制输入；其他长度不解析，rgb 保持 0。
 *   颜色通道按 [0,1] 浮点输出，与 Color 结构体约定一致。
 *
 ******************************************************************************/
#pragma once
#include "Render2D/DrawList.h"
#include <string_view>
#include <charconv>
namespace ih::render2d {
// Hex: 把 "#RRGGBB" 形式的十六进制颜色串解析为 [0,1] 归一化 Color；alpha 由调用方指定。
// 失败时 rgb 保持 0（黑），不抛异常，便于在 UI 配置等容错场景使用。
inline Color Hex(std::string_view text, float alpha = 1) {
    unsigned rgb{};
// 只接受长度 7 且以 '#' 开头的输入；from_chars 无异常、不分配，适合频繁调用。
    if (text.size() == 7 && text[0] == '#') std::from_chars(text.data() + 1, text.data() + 7, rgb, 16);
    return {((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, alpha};
}
}
