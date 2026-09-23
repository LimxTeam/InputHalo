#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include "Text/FontAtlas.h"

namespace ih::render2d {
struct Point { float x{}, y{}; };
struct Rect { float x{}, y{}, width{}, height{}; };
struct Color { float r{}, g{}, b{}, a{1}; };
enum class Shape : uint32_t { Rectangle, RoundedRectangle, Circle, Line, Glyph, Ring, Glow };
// Four float4s, shared with HLSL. Geometry is in DIP; color is straight sRGB.
struct Command {
    Rect bounds;
    Color color;
    std::array<float, 4> parameters{}; // type, radius/half stroke width, atlas page, reserved
    std::array<float, 4> endpoints{}; // line endpoints or glyph UV rectangle
};
static_assert(sizeof(Command) == 64);
struct Batch { uint32_t first{}, count{}; Rect clip; };
struct DrawList {
    std::vector<Command> commands;
    std::vector<Batch> batches;
    std::shared_ptr<const text::Atlas> atlas;
};
inline constexpr uint32_t MaxCommands = 4096;
}
