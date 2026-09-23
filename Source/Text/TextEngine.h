/*******************************************************************************
 * 文件: TextEngine.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   声明文本引擎 TextEngine：字体注册、UTF-8 文本塑形(shape)与字形栅格化进图集。
 *   接口仅在 UI 线程使用，输出 DIP 坐标的排版结果。不涉及复杂算法。
 *
 * 设计哲学:
 *   pImpl 不透明指针隔离 FreeType/HarfBuzz 细节，头文件不暴露第三方类型；
 *   图集快照不可变，渲染侧持引用后无需加锁。
 *
 * 技术特性:
 *   - AddFont 按调用顺序建立字体回退链
 *   - Shape 输出 DIP 坐标的 Layout
 *   - 默认图集 1024、最多 8 页
 *
 * 依赖关系:
 *   内部: Text/FontAtlas.h
 *   外部: <filesystem>、<string_view>；实现层依赖 FreeType、HarfBuzz
 *
 * 注意事项:
 *   UI 线程封闭；Shape 坐标为 DIP，需配合 dpiScale 换算设备像素。
 ******************************************************************************/
#pragma once
#include "Text/FontAtlas.h"
#include <filesystem>
#include <string_view>
// InputHalo 文本层命名空间。
namespace ih::text {
// UI-thread confined. Layout coordinates are DIP. Retained atlas snapshots are immutable.
// 文本引擎：UI 线程封闭。排版坐标为 DIP；图集快照一旦产出即不可变。
class TextEngine {
public:
// 构造：图集页边长与最大页数。
    explicit TextEngine(uint32_t atlasSize = 1024, uint32_t maxPages = 8);
// 析构(经 pImpl 释放 FreeType/HarfBuzz 资源)。
    ~TextEngine();
// 禁拷贝禁赋值：引擎持有原生库句柄与图集，不可复制。
    TextEngine(const TextEngine&) = delete;
    TextEngine& operator=(const TextEngine&) = delete;
// 注册字体文件(按顺序作为回退)，faceIndex 指定字体集合中的面。
    void AddFont(const std::filesystem::path& path, long faceIndex = 0);
// 对 UTF-8 文本塑形排版，sizeDip 为逻辑字号，dpiScale 为 DPI 缩放。
    Layout Shape(std::string_view utf8, float sizeDip, float dpiScale = 1);
private:
// 不实现细节：FreeType/HarfBuzz 状态隐藏在此结构中。
    struct Impl;
// pImpl 指针。
    std::unique_ptr<Impl> m_impl;
};
}
