#include "Text/TextEngine.h"
#include "Platform/Windows/Win32Fonts.h"
#include "Render2D/Painter.h"
#include <iostream>
#include <cmath>
#include <stdexcept>
void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int main() {
    try {
        const auto fonts = ih::platform::DefaultFontPaths();
        ih::text::TextEngine engine;
        for (const auto& font : fonts) engine.AddFont(font);
        const auto ascii = engine.Shape("InputHalo office AV 0123456789", 20);
        Require(!ascii.glyphs.empty() && !ascii.missingGlyphs && ascii.width > 50, "ASCII shaping");
        const auto cached = engine.Shape("InputHalo office AV 0123456789", 20);
        Require(cached.atlas == ascii.atlas, "Cached glyphs must not dirty atlas");
        const auto oldRevision = ascii.atlas->revision;
        const auto unicode = engine.Shape("输入光晕 · 中文\nΚαλημέρα العربية", 20);
        Require(!unicode.missingGlyphs && unicode.height > ascii.height, "Unicode fallback and multiline");
        Require(ascii.atlas->revision == oldRevision && unicode.atlas->revision > oldRevision, "Retained atlas snapshot must be immutable");
        const auto composed = engine.Shape("e\xcc\x81", 20);
        Require(!composed.missingGlyphs && composed.glyphs.size() <= 2, "Combining mark shaping");
        const auto arabic = engine.Shape("العربية", 20);
        Require(!arabic.missingGlyphs && arabic.glyphs.size() > 1 && arabic.glyphs.front().cluster > arabic.glyphs.back().cluster, "RTL run must be shaped in visual order");
        for (float scale : {1.f, 1.25f, 1.5f, 2.f}) {
            const auto layout = engine.Shape("InputHalo 输入光晕", 20, scale);
            Require(!layout.missingGlyphs && layout.width > 20 && layout.atlas->size == 2048, "DPI text layout");
        }
        Require(engine.Shape("", 20).glyphs.empty(), "Empty text");
        Require(engine.Shape("   ", 20).glyphs.empty() && engine.Shape("   ", 20).width > 0, "Spaces advance without bitmap");
        bool invalid = false;
        try { engine.Shape("\xf0\x80\x80\x80", 20); } catch (const std::invalid_argument&) { invalid = true; }
        Require(invalid, "Overlong UTF-8 must be rejected");
        Require(engine.Shape("\xf4\x8f\xbf\xbf", 20).missingGlyphs == 1, "Missing glyph must be reported");
        ih::text::TextEngine small(64, 8);
        small.AddFont(fonts.front());
        const auto pages = small.Shape("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", 18);
        Require(pages.atlas->pages.size() > 1, "Atlas must add pages");
        ih::render2d::Painter painter(500, 300);
        painter.Text(ascii, {0, 0}, {1, 1, 1, 1});
        painter.Text(unicode, {0, 50}, {1, 1, 1, 1});
        Require(painter.List().atlas == unicode.atlas && painter.List().batches.size() == 1, "Atlas revision upgrade and text batching");
        std::cout << "Text: ASCII, Unicode fallback, RTL run, combining, UTF-8 validation, cache, DPI, snapshots and atlas growth passed.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
