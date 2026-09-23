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

namespace ih::text {
namespace {
struct Character { uint32_t code; unsigned start, end; };
std::vector<Character> Decode(std::string_view input) {
    if (input.size() > 1024 * 1024) throw std::length_error("Text input exceeds 1 MiB");
    std::vector<Character> output;
    for (unsigned i = 0; i < input.size();) {
        const unsigned start = i;
        const auto first = static_cast<unsigned char>(input[i++]);
        uint32_t code = first; unsigned extra = 0; uint32_t minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) { code &= 31; extra = 1; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { code &= 15; extra = 2; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { code &= 7; extra = 3; minimum = 0x10000; }
        else if (first >= 0x80) throw std::invalid_argument("Invalid UTF-8 lead byte");
        for (unsigned n = 0; n < extra; ++n) {
            if (i == input.size()) throw std::invalid_argument("Truncated UTF-8");
            const auto next = static_cast<unsigned char>(input[i++]);
            if ((next & 0xc0) != 0x80) throw std::invalid_argument("Invalid UTF-8 continuation");
            code = (code << 6) | (next & 63);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) throw std::invalid_argument("Invalid Unicode scalar");
        output.push_back({code, start, i});
    }
    return output;
}
void Ft(FT_Error result, const char* operation) { if (result) throw std::runtime_error(std::string(operation) + ": " + std::to_string(result)); }
}
struct TextEngine::Impl {
    struct Font {
        hb_blob_t* data{};
        FT_Face face{};
        hb_font_t* shaper{};
        ~Font() { if (shaper) hb_font_destroy(shaper); if (face) FT_Done_Face(face); if(data) hb_blob_destroy(data); }
    };
    struct Cached { uint32_t page{}, x{}, y{}, width{}, height{}; int left{}, top{}; };
    FT_Library library{};
    std::vector<std::unique_ptr<Font>> fonts;
    std::shared_ptr<Atlas> atlas = std::make_shared<Atlas>();
    std::unordered_map<uint64_t, Cached> cache;
    uint32_t maximum{}, cursorX = 1, cursorY = 1, rowHeight{};
    ~Impl() { fonts.clear(); if (library) FT_Done_FreeType(library); }
    Cached Raster(unsigned fontIndex, uint32_t glyph, uint32_t size) {
        const uint64_t key = (static_cast<uint64_t>(fontIndex) << 48) | (static_cast<uint64_t>(size) << 32) | glyph;
        if (const auto found = cache.find(key); found != cache.end()) return found->second;
        auto face = fonts[fontIndex]->face;
        Ft(FT_Load_Glyph(face, glyph, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP), "Load glyph");
        Ft(FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL), "Rasterize glyph");
        const auto& bitmap = face->glyph->bitmap;
        Cached entry{};
        entry.width = bitmap.width; entry.height = bitmap.rows;
        entry.left = face->glyph->bitmap_left; entry.top = face->glyph->bitmap_top;
        if (!entry.width || !entry.height) { cache.emplace(key, entry); return entry; }
        if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY || bitmap.num_grays != 256) throw std::runtime_error("Expected grayscale font glyph");
        if (entry.width + 2 > atlas->size || entry.height + 2 > atlas->size) throw std::length_error("Glyph exceeds atlas page");
        if (atlas.use_count() > 1) atlas = std::make_shared<Atlas>(*atlas);
        if (cursorX + entry.width + 1 > atlas->size) { cursorX = 1; cursorY += rowHeight + 1; rowHeight = 0; }
        if (cursorY + entry.height + 1 > atlas->size) {
            if (atlas->pages.size() >= maximum) throw std::length_error("Font atlas page limit reached");
            atlas->pages.emplace_back(static_cast<size_t>(atlas->size) * atlas->size, uint8_t{0});
            cursorX = cursorY = 1; rowHeight = 0;
        }
        entry.page = static_cast<uint32_t>(atlas->pages.size() - 1); entry.x = cursorX; entry.y = cursorY;
        auto& page = atlas->pages.back();
        for (uint32_t row = 0; row < entry.height; ++row) {
            const auto sourceRow = bitmap.pitch >= 0 ? row : entry.height - 1 - row;
            const auto* source = bitmap.buffer + static_cast<size_t>(sourceRow) * static_cast<size_t>(std::abs(bitmap.pitch));
            std::copy_n(source, entry.width, page.begin() + static_cast<size_t>(entry.y + row) * atlas->size + entry.x);
        }
        cursorX += entry.width + 1; rowHeight = std::max(rowHeight, entry.height);
        ++atlas->revision;
        cache.emplace(key, entry);
        return entry;
    }
};
TextEngine::TextEngine(uint32_t size, uint32_t maxPages) : m_impl(std::make_unique<Impl>()) {
    if (size < 32 || size > 4096 || maxPages < 1 || maxPages > 8) throw std::invalid_argument("Invalid atlas limits");
    Ft(FT_Init_FreeType(&m_impl->library), "Initialize FreeType");
    static std::atomic<uint64_t> identity{};
    m_impl->atlas->identity = ++identity; m_impl->atlas->size = size;
    m_impl->atlas->pages.emplace_back(static_cast<size_t>(size) * size, uint8_t{0}); m_impl->maximum = maxPages;
}
TextEngine::~TextEngine() = default;
void TextEngine::AddFont(const std::filesystem::path& path, long faceIndex) {
    if (m_impl->fonts.size() >= 64) throw std::length_error("Too many fallback fonts");
    auto font = std::make_unique<Impl::Font>();
    const auto size = std::filesystem::file_size(path);
    if (size <= 0 || size > 128 * 1024 * 1024) throw std::runtime_error("Invalid font size");
    // HarfBuzz maps Windows files read-only. Both engines share the mapping for the font lifetime.
    const auto utf8=path.u8string();
    font->data=hb_blob_create_from_file_or_fail(reinterpret_cast<const char*>(utf8.c_str()));
    if(!font->data)throw std::runtime_error("Cannot map font file");
    unsigned length{};const char* data=hb_blob_get_data(font->data,&length);
    if(length!=size)throw std::runtime_error("Font file changed while opening");
    Ft(FT_New_Memory_Face(m_impl->library, reinterpret_cast<const FT_Byte*>(data),
        static_cast<FT_Long>(size), faceIndex, &font->face), "Open font");
    Ft(FT_Select_Charmap(font->face, FT_ENCODING_UNICODE), "Select Unicode cmap");
    auto* face = hb_face_create(font->data, static_cast<unsigned>(faceIndex));
    font->shaper = hb_font_create(face); hb_face_destroy(face);
    hb_ot_font_set_funcs(font->shaper);
    m_impl->fonts.push_back(std::move(font));
}
Layout TextEngine::Shape(std::string_view utf8, float sizeDip, float scale) {
    if (m_impl->fonts.empty()) throw std::logic_error("No fonts loaded");
    if (!std::isfinite(sizeDip) || !std::isfinite(scale) || sizeDip <= 0 || scale <= 0 || sizeDip * scale > 512)
        throw std::invalid_argument("Invalid text size/scale");
    const auto characters = Decode(utf8);
    const auto pixels = static_cast<uint32_t>(std::max(1.f, std::round(sizeDip * scale)));
    float ascender = 0, lineHeight = 0;
    for (auto& font : m_impl->fonts) {
        Ft(FT_Set_Pixel_Sizes(font->face, 0, pixels), "Set font pixel size");
        hb_font_set_scale(font->shaper, static_cast<int>(pixels * 64), static_cast<int>(pixels * 64));
        hb_font_set_ppem(font->shaper, pixels, pixels);
        ascender = std::max(ascender, font->face->size->metrics.ascender / 64.f);
        lineHeight = std::max(lineHeight, font->face->size->metrics.height / 64.f);
    }
    lineHeight = std::max(lineHeight, static_cast<float>(pixels));
    Layout result;
    float penX = 0, baseline = ascender;
    auto* unicode = hb_unicode_funcs_get_default();
    for (size_t start = 0; start < characters.size();) {
        if (characters[start].code == '\n') { result.width = std::max(result.width, penX / scale); penX = 0; baseline += lineHeight; ++start; continue; }
        if (characters[start].code == '\r') { ++start; continue; }
        const auto selectFont = [&](uint32_t code) {
            for (unsigned i = 0; i < m_impl->fonts.size(); ++i) if (FT_Get_Char_Index(m_impl->fonts[i]->face, code)) return i;
            return 0u;
        };
        const unsigned fontIndex = selectFont(characters[start].code);
        auto script = hb_unicode_script(unicode, characters[start].code);
        size_t end = start + 1;
        for (; end < characters.size(); ++end) {
            const auto code = characters[end].code;
            if (code == '\n' || code == '\r' || selectFont(code) != fontIndex) break;
            const auto nextScript = hb_unicode_script(unicode, code);
            if (nextScript == HB_SCRIPT_COMMON || nextScript == HB_SCRIPT_INHERITED) continue;
            if (script == HB_SCRIPT_COMMON || script == HB_SCRIPT_INHERITED) script = nextScript;
            else if (script != nextScript) break;
        }
        std::unique_ptr<hb_buffer_t, decltype(&hb_buffer_destroy)> buffer(hb_buffer_create(), hb_buffer_destroy);
        hb_buffer_add_utf8(buffer.get(), utf8.data(), static_cast<int>(utf8.size()), characters[start].start,
            static_cast<int>(characters[end - 1].end - characters[start].start));
        hb_buffer_set_script(buffer.get(), script); hb_buffer_guess_segment_properties(buffer.get());
        hb_shape(m_impl->fonts[fontIndex]->shaper, buffer.get(), nullptr, 0);
        if (!hb_buffer_allocation_successful(buffer.get())) throw std::bad_alloc();
        unsigned count{};
        const auto* infos = hb_buffer_get_glyph_infos(buffer.get(), &count);
        const auto* positions = hb_buffer_get_glyph_positions(buffer.get(), nullptr);
        for (unsigned i = 0; i < count; ++i) {
            const auto entry = m_impl->Raster(fontIndex, infos[i].codepoint, pixels);
            if (!infos[i].codepoint) ++result.missingGlyphs;
            if (entry.width && entry.height) {
                const float atlasSize = static_cast<float>(m_impl->atlas->size);
                result.glyphs.push_back({(penX + positions[i].x_offset / 64.f + entry.left) / scale,
                    (baseline - positions[i].y_offset / 64.f - entry.top) / scale, entry.width / scale, entry.height / scale,
                    entry.x / atlasSize, entry.y / atlasSize, (entry.x + entry.width) / atlasSize, (entry.y + entry.height) / atlasSize,
                    entry.page, infos[i].codepoint, infos[i].cluster});
            }
            penX += positions[i].x_advance / 64.f;
        }
        start = end;
    }
    result.width = std::max(result.width, penX / scale);
    result.height = characters.empty() ? 0 : (baseline - ascender + lineHeight) / scale;
    result.atlas = m_impl->atlas;
    return result;
}
}
