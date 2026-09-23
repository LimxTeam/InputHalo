#pragma once
#include "Render2D/DrawList.h"
#include <string_view>
#include <charconv>
namespace ih::render2d {
inline Color Hex(std::string_view text, float alpha = 1) {
    unsigned rgb{};
    if (text.size() == 7 && text[0] == '#') std::from_chars(text.data() + 1, text.data() + 7, rgb, 16);
    return {((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, alpha};
}
}
