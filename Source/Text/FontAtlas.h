/*******************************************************************************
 * 文件: FontAtlas.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   声明字体图集、字形与文本排版的纯数据结构。
 *   Atlas 为多页 8 位灰度位图；Glyph 记录图集 UV 与字形度量；Layout 是一次排版产出的字形序列。
 *   不涉及复杂算法，仅数据布局。
 *
 * 设计哲学:
 *   图集快照不可变：渲染期以 shared_ptr<const Atlas> 共享，
 *   新增字形时整体复制写时分离，避免与 GPU 上传发生竞态。
 *
 * 技术特性:
 *   - identity/revision 双 64 位版本号，供 GPU 增量上传判定变化
 *   - pages 为逐页字节缓冲，支持多页翻页
 *   - missingGlyphs 统计缺字数
 *
 * 依赖关系:
 *   外部: <cstdint>、<vector>、<memory>
 *
 * 注意事项:
 *   坐标为 DIP 逻辑像素；UV 已归一化到图集尺寸。
 ******************************************************************************/
#pragma once
#include <cstdint>
#include <vector>
#include <memory>
// InputHalo 文本层命名空间。
namespace ih::text {
// 字体图集：多页灰度位图及其版本身份。
struct Atlas {
// identity 标识图集实例，revision 每次新增字形自增，供渲染层检测脏区域。
    uint64_t identity{}, revision{};
// 单页边长(像素)，默认 2048。
    uint32_t size = 2048;
// 逐页像素缓冲，每页 size×size 字节。
    std::vector<std::vector<uint8_t>> pages;
};
// 单个字形：图集内位置/尺寸、归一化 UV、所属页与字形索引。
struct Glyph {
// 字形在排版中的原点偏移与宽高(DIP)。
    float x{}, y{}, width{}, height{};
// 图集纹理坐标 [u,v] 到 [uEnd,vEnd](已归一化)。
    float u{}, v{}, uEnd{}, vEnd{};
// 所属图集页、字形码位、源字符簇(cluster)。
    uint32_t page{}, glyphIndex{}, cluster{};
};
// 一次 Shape 排版的结果：字形序列 + 引用的图集快照 + 整体尺寸。
struct Layout {
// 排好的字形 quad 列表。
    std::vector<Glyph> glyphs;
// 共享的不可变图集快照。
    std::shared_ptr<const Atlas> atlas;
// 整体宽高(DIP)。
    float width{}, height{};
// 排版中缺失(无对应字形)的字符数。
    uint32_t missingGlyphs{};
};
}
