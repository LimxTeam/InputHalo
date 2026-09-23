#pragma once
#include "Text/FontAtlas.h"
#include <filesystem>
#include <string_view>
namespace ih::text {
// UI-thread confined. Layout coordinates are DIP. Retained atlas snapshots are immutable.
class TextEngine {
public:
    explicit TextEngine(uint32_t atlasSize = 1024, uint32_t maxPages = 8);
    ~TextEngine();
    TextEngine(const TextEngine&) = delete;
    TextEngine& operator=(const TextEngine&) = delete;
    void AddFont(const std::filesystem::path& path, long faceIndex = 0);
    Layout Shape(std::string_view utf8, float sizeDip, float dpiScale = 1);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
