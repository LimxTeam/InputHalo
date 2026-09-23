#pragma once
#include <cstdint>
#include <vector>
#include <memory>
namespace ih::text {
struct Atlas {
    uint64_t identity{}, revision{};
    uint32_t size = 2048;
    std::vector<std::vector<uint8_t>> pages;
};
struct Glyph {
    float x{}, y{}, width{}, height{};
    float u{}, v{}, uEnd{}, vEnd{};
    uint32_t page{}, glyphIndex{}, cluster{};
};
struct Layout {
    std::vector<Glyph> glyphs;
    std::shared_ptr<const Atlas> atlas;
    float width{}, height{};
    uint32_t missingGlyphs{};
};
}
