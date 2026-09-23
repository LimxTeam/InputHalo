/*******************************************************************************
 * 文件: TextEngine.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   实现 UTF-8 解码、FreeType 字形加载/栅格化、HarfBuzz 塑形、图集矩形打包与字形缓存。
 *   Decode 把 UTF-8 流转为 Unicode 标量序列；Raster 按 (字体,字号,码位) 缓存并紧排灰度位图；
 *   Shape 按字体/脚本切段塑形后输出 DIP 字形 quad。
 *   复杂度: 排版 O(字符数 + 命中缓存的字形数)；缓存查找 O(1)。
 *
 * 设计哲学:
 *   回退策略即按字体顺序取首个含该码位的 face；Common/Inherited 脚本字符并入上一段，
 *   保证同一字体连续塑形；图集写时复制，渲染快照不被后续栅格化污染。
 *
 * 技术特性:
 *   - FreeType 以 NO_HINTING 渲染 256 级灰度位图
 *   - 图集矩形打包(cursorX/cursorY 换行翻页)
 *   - 缓存键 = fontIndex<<48 | size<<32 | glyph，uint64 唯一
 *   - 缺字(codepoint==0)计数到 missingGlyphs
 *
 * 依赖关系:
 *   外部: FreeType(ft2build)、HarfBuzz(hb/hb-ot)、<unordered_map>、<atomic>
 *
 * 注意事项:
 *   单文本 ≤1 MiB；像素尺寸=round(sizeDip*scale) 且 ≤512；字体 ≤64 个、单文件 ≤128 MiB。
 ******************************************************************************/
#include "Text/TextEngine.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ot.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <unordered_map>
#include <stdexcept>

// InputHalo 文本层命名空间实现。
namespace ih::text {
// 匿名命名空间：仅本文件使用的 UTF-8 解码与 FreeType 错误辅助。
namespace {
// 解码后的字符：Unicode 码位 + 其在原串中的字节区间[start,end)。
struct Character { uint32_t code; unsigned start, end; };
// UTF-8 -> Unicode 标量序列；严格校验引导字节、续字节与非法标量。
std::vector<Character> Decode(std::string_view input) {
// 输入上限 1 MiB，防止超长文本。
    if (input.size() > 1024 * 1024) throw std::length_error("Text input exceeds 1 MiB");
    std::vector<Character> output;
// 逐字节扫描。
    for (unsigned i = 0; i < input.size();) {
        const unsigned start = i;
// 取首字节并按 UTF-8 长度类别解析。
        const auto first = static_cast<unsigned char>(input[i++]);
        uint32_t code = first; unsigned extra = 0; uint32_t minimum = 0;
// 2/3/4 字节序列：剥离前缀位，记录续字节数与最小合法值。
        if (first >= 0xc2 && first <= 0xdf) { code &= 31; extra = 1; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { code &= 15; extra = 2; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { code &= 7; extra = 3; minimum = 0x10000; }
// 独立续字节或非法引导字节直接报错。
        else if (first >= 0x80) throw std::invalid_argument("Invalid UTF-8 lead byte");
// 读取并校验续字节(必须 10xxxxxx 形式)。
        for (unsigned n = 0; n < extra; ++n) {
            if (i == input.size()) throw std::invalid_argument("Truncated UTF-8");
            const auto next = static_cast<unsigned char>(input[i++]);
            if ((next & 0xc0) != 0x80) throw std::invalid_argument("Invalid UTF-8 continuation");
            code = (code << 6) | (next & 63);
        }
// 拒绝过短/超范围/代理区的非法 Unicode 标量。
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) throw std::invalid_argument("Invalid Unicode scalar");
        output.push_back({code, start, i});
    }
    return output;
}
// FreeType 错误检查宏：非 0 即抛异常并附带操作名。
void Ft(FT_Error result, const char* operation) { if (result) throw std::runtime_error(std::string(operation) + ": " + std::to_string(result)); }
}
// pImpl 实现体。
struct TextEngine::Impl {
// 单个字体：HarfBuzz 数据映射、FreeType face、HarfBuzz shaper。
    struct Font {
// 内存映射的字体数据(HarfBuzz 持有，生命周期内只读)。
        hb_blob_t* data{};
// FreeType 字体面。
        FT_Face face{};
// HarfBuzz 塑形器。
        hb_font_t* shaper{};
// 逆序释放：先 shaper，再 face，最后数据映射。
        ~Font() { if (shaper) hb_font_destroy(shaper); if (face) FT_Done_Face(face); if(data) hb_blob_destroy(data); }
    };
// 已栅格化字形在图集中的放置结果。
    struct Cached { uint32_t page{}, x{}, y{}, width{}, height{}; int left{}, top{}; };
// FreeType 库句柄。
    FT_Library library{};
// 已加载的回退字体列表(顺序即回退优先级)。
    std::vector<std::unique_ptr<Font>> fonts;
// 当前图集快照(可能被写时复制替换)。
    std::shared_ptr<Atlas> atlas = std::make_shared<Atlas>();
// 字形缓存：键 -> 放置信息。
    std::unordered_map<uint64_t, Cached> cache;
// 打包光标与本页最大行高。
    uint32_t maximum{}, cursorX = 1, cursorY = 1, rowHeight{};
// 释放所有字体并关闭 FreeType 库。
    ~Impl() { fonts.clear(); if (library) FT_Done_FreeType(library); }
// 栅格化并缓存单个字形；命中缓存则直接返回。
    Cached Raster(unsigned fontIndex, uint32_t glyph, uint32_t size) {
// 构造 uint64 缓存键。
        const uint64_t key = (static_cast<uint64_t>(fontIndex) << 48) | (static_cast<uint64_t>(size) << 32) | glyph;
// 命中缓存则直接返回。
        if (const auto found = cache.find(key); found != cache.end()) return found->second;
        auto face = fonts[fontIndex]->face;
// 加载字形(不 hinting、不取位图)。
        Ft(FT_Load_Glyph(face, glyph, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP), "Load glyph");
// 栅格化为灰度位图。
        Ft(FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL), "Rasterize glyph");
        const auto& bitmap = face->glyph->bitmap;
        Cached entry{};
// 记录位图尺寸与笔偏移(left/top)。
        entry.width = bitmap.width; entry.height = bitmap.rows;
        entry.left = face->glyph->bitmap_left; entry.top = face->glyph->bitmap_top;
// 空白字形(零宽)也入缓存，避免重复加载。
        if (!entry.width || !entry.height) { cache.emplace(key, entry); return entry; }
// 仅接受 256 级灰度像素格式。
        if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY || bitmap.num_grays != 256) throw std::runtime_error("Expected grayscale font glyph");
// 字形过大(加内边距)放不下单页则报错。
        if (entry.width + 2 > atlas->size || entry.height + 2 > atlas->size) throw std::length_error("Glyph exceeds atlas page");
// 若图集快照正被外部共享，则先整体复制再写入(写时复制)。
        if (atlas.use_count() > 1) atlas = std::make_shared<Atlas>(*atlas);
// 行内放不下则换行。
        if (cursorX + entry.width + 1 > atlas->size) { cursorX = 1; cursorY += rowHeight + 1; rowHeight = 0; }
// 纵向放不下则新建一页；超过页数上限报错。
        if (cursorY + entry.height + 1 > atlas->size) {
            if (atlas->pages.size() >= maximum) throw std::length_error("Font atlas page limit reached");
// 新建一页全零缓冲。
            atlas->pages.emplace_back(static_cast<size_t>(atlas->size) * atlas->size, uint8_t{0});
            cursorX = cursorY = 1; rowHeight = 0;
        }
// 记录字形所属页与放置坐标。
        entry.page = static_cast<uint32_t>(atlas->pages.size() - 1); entry.x = cursorX; entry.y = cursorY;
        auto& page = atlas->pages.back();
// 逐行把灰度像素拷贝进图集页(处理位图可能倒序的 pitch)。
        for (uint32_t row = 0; row < entry.height; ++row) {
            const auto sourceRow = bitmap.pitch >= 0 ? row : entry.height - 1 - row;
            const auto* source = bitmap.buffer + static_cast<size_t>(sourceRow) * static_cast<size_t>(std::abs(bitmap.pitch));
            std::copy_n(source, entry.width, page.begin() + static_cast<size_t>(entry.y + row) * atlas->size + entry.x);
        }
// 推进打包光标，更新行高。
        cursorX += entry.width + 1; rowHeight = std::max(rowHeight, entry.height);
// 版本号自增，通知渲染层图集已变。
        ++atlas->revision;
        cache.emplace(key, entry);
        return entry;
    }
};
// 构造：校验图集参数并初始化 FreeType。
TextEngine::TextEngine(uint32_t size, uint32_t maxPages) : m_impl(std::make_unique<Impl>()) {
// 图集尺寸与页数合法性检查。
    if (size < 32 || size > 4096 || maxPages < 1 || maxPages > 8) throw std::invalid_argument("Invalid atlas limits");
// 初始化 FreeType 库。
    Ft(FT_Init_FreeType(&m_impl->library), "Initialize FreeType");
// 进程内自增的图集身份号(原子)。
    static std::atomic<uint64_t> identity{};
// 设定图集身份与边长。
    m_impl->atlas->identity = ++identity; m_impl->atlas->size = size;
// 建立首页全零缓冲并记录页数上限。
    m_impl->atlas->pages.emplace_back(static_cast<size_t>(size) * size, uint8_t{0}); m_impl->maximum = maxPages;
}
// 析构默认实现(Impl 已自行清理)。
TextEngine::~TextEngine() = default;
// 注册字体：建立共享只读映射，并同时供 FreeType 与 HarfBuzz 使用。
void TextEngine::AddFont(const std::filesystem::path& path, long faceIndex) {
// 回退字体最多 64 个。
    if (m_impl->fonts.size() >= 64) throw std::length_error("Too many fallback fonts");
    auto font = std::make_unique<Impl::Font>();
// 读取字体文件大小。
    const auto size = std::filesystem::file_size(path);
// 拒绝空文件或超过 128 MiB 的字体。
    if (size <= 0 || size > 128 * 1024 * 1024) throw std::runtime_error("Invalid font size");
    // HarfBuzz maps Windows files read-only. Both engines share the mapping for the font lifetime.
    const auto utf8=path.u8string();
// 由路径创建只读内存映射。
    font->data=hb_blob_create_from_file_or_fail(reinterpret_cast<const char*>(utf8.c_str()));
// 映射失败则报错。
    if(!font->data)throw std::runtime_error("Cannot map font file");
// 取映射指针与长度。
    unsigned length{};const char* data=hb_blob_get_data(font->data,&length);
// 长度与文件大小不一致说明文件被改动，拒绝。
    if(length!=size)throw std::runtime_error("Font file changed while opening");
// 在该内存上创建 FreeType face(共享同一份映射)。
    Ft(FT_New_Memory_Face(m_impl->library, reinterpret_cast<const FT_Byte*>(data),
        static_cast<FT_Long>(size), faceIndex, &font->face), "Open font");
// 选择 Unicode 字符映射表。
    Ft(FT_Select_Charmap(font->face, FT_ENCODING_UNICODE), "Select Unicode cmap");
// 由同一映射创建 HarfBuzz face 与 shaper。
    auto* face = hb_face_create(font->data, static_cast<unsigned>(faceIndex));
    font->shaper = hb_font_create(face); hb_face_destroy(face);
// 启用 OpenType 塑形函数。
    hb_ot_font_set_funcs(font->shaper);
    m_impl->fonts.push_back(std::move(font));
}
// 文本排版主入口：解码、设置字号、分段塑形、栅格化、输出 DIP quad。
Layout TextEngine::Shape(std::string_view utf8, float sizeDip, float scale) {
// 至少要加载一个字体。
    if (m_impl->fonts.empty()) throw std::logic_error("No fonts loaded");
// 字号与缩放必须为正有限，且设备像素上限 512。
    if (!std::isfinite(sizeDip) || !std::isfinite(scale) || sizeDip <= 0 || scale <= 0 || sizeDip * scale > 512)
        throw std::invalid_argument("Invalid text size/scale");
// 解码 UTF-8。
    const auto characters = Decode(utf8);
// 换算目标设备像素尺寸。
    const auto pixels = static_cast<uint32_t>(std::max(1.f, std::round(sizeDip * scale)));
// 遍历字体取最大上伸与行高，保证回退字体对齐。
    float ascender = 0, lineHeight = 0;
    for (auto& font : m_impl->fonts) {
// 为每个字体设置像素尺寸。
        Ft(FT_Set_Pixel_Sizes(font->face, 0, pixels), "Set font pixel size");
// 同步 HarfBuzz 缩放(26.6 定点)。
        hb_font_set_scale(font->shaper, static_cast<int>(pixels * 64), static_cast<int>(pixels * 64));
        hb_font_set_ppem(font->shaper, pixels, pixels);
// 累计最大上伸/行高。
        ascender = std::max(ascender, font->face->size->metrics.ascender / 64.f);
        lineHeight = std::max(lineHeight, font->face->size->metrics.height / 64.f);
    }
// 行高至少不小于字号像素。
    lineHeight = std::max(lineHeight, static_cast<float>(pixels));
    Layout result;
// 排版笔位置：penX 横向，baseline 纵向基线。
    float penX = 0, baseline = ascender;
    auto* unicode = hb_unicode_funcs_get_default();
// 逐段排版循环。
    for (size_t start = 0; start < characters.size();) {
// 换行符：结算当前行宽并下移基线。
        if (characters[start].code == '\n') { result.width = std::max(result.width, penX / scale); penX = 0; baseline += lineHeight; ++start; continue; }
// 回车符：忽略。
        if (characters[start].code == '\r') { ++start; continue; }
// 回退选择 lambda：返回首个含该码位的字体索引，缺省 0。
        const auto selectFont = [&](uint32_t code) {
            for (unsigned i = 0; i < m_impl->fonts.size(); ++i) if (FT_Get_Char_Index(m_impl->fonts[i]->face, code)) return i;
            return 0u;
        };
// 按首字符选定本段字体。
        const unsigned fontIndex = selectFont(characters[start].code);
        auto script = hb_unicode_script(unicode, characters[start].code);
// 向前扩展本段：直到换行、字体变化或脚本切换。
        size_t end = start + 1;
        for (; end < characters.size(); ++end) {
            const auto code = characters[end].code;
            if (code == '\n' || code == '\r' || selectFont(code) != fontIndex) break;
// Common/Inherited 脚本并入当前段，避免中英文混排被错误切分。
            const auto nextScript = hb_unicode_script(unicode, code);
            if (nextScript == HB_SCRIPT_COMMON || nextScript == HB_SCRIPT_INHERITED) continue;
            if (script == HB_SCRIPT_COMMON || script == HB_SCRIPT_INHERITED) script = nextScript;
            else if (script != nextScript) break;
        }
// 创建 HarfBuzz 缓冲区。
        std::unique_ptr<hb_buffer_t, decltype(&hb_buffer_destroy)> buffer(hb_buffer_create(), hb_buffer_destroy);
// 填入本段 UTF-8 区间。
        hb_buffer_add_utf8(buffer.get(), utf8.data(), static_cast<int>(utf8.size()), characters[start].start,
            static_cast<int>(characters[end - 1].end - characters[start].start));
// 设定脚本并猜测其余段属性，然后塑形。
        hb_buffer_set_script(buffer.get(), script); hb_buffer_guess_segment_properties(buffer.get());
        hb_shape(m_impl->fonts[fontIndex]->shaper, buffer.get(), nullptr, 0);
// 缓冲区分配失败检查。
        if (!hb_buffer_allocation_successful(buffer.get())) throw std::bad_alloc();
        unsigned count{};
// 取塑形后的字形信息与位置。
        const auto* infos = hb_buffer_get_glyph_infos(buffer.get(), &count);
        const auto* positions = hb_buffer_get_glyph_positions(buffer.get(), nullptr);
// 逐字形输出 quad。
        for (unsigned i = 0; i < count; ++i) {
// 栅格化(或命中缓存)。
            const auto entry = m_impl->Raster(fontIndex, infos[i].codepoint, pixels);
// codepoint 为 0 视为缺字并计数。
            if (!infos[i].codepoint) ++result.missingGlyphs;
            if (entry.width && entry.height) {
// 计算归一化 UV。
                const float atlasSize = static_cast<float>(m_impl->atlas->size);
// 输出 DIP 字形 quad：含笔偏移、基线、UV、页与簇信息。
                result.glyphs.push_back({(penX + positions[i].x_offset / 64.f + entry.left) / scale,
                    (baseline - positions[i].y_offset / 64.f - entry.top) / scale, entry.width / scale, entry.height / scale,
                    entry.x / atlasSize, entry.y / atlasSize, (entry.x + entry.width) / atlasSize, (entry.y + entry.height) / atlasSize,
                    entry.page, infos[i].codepoint, infos[i].cluster});
            }
// 横向前进到下一笔位置。
            penX += positions[i].x_advance / 64.f;
        }
        start = end;
    }
// 结算最终行宽。
    result.width = std::max(result.width, penX / scale);
// 总高 = 首行基线到末行的距离 / scale。
    result.height = characters.empty() ? 0 : (baseline - ascender + lineHeight) / scale;
// 绑定本次产出的图集快照。
    result.atlas = m_impl->atlas;
    return result;
}
}
