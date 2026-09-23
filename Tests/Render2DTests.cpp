#include "Render2D/Painter.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace ih::render2d;
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        Painter painter(200, 100);
        painter.FillRect({0, 0, 50, 50}, {1, 0, 0, .5f});
        painter.Circle({20, 20}, 10, {0, 1, 0, .5f});
        Require(painter.List().batches.size() == 1 && painter.List().batches[0].count == 2, "Shapes must batch without reordering");
        painter.PushClip({10, 10, 30, 30});
        painter.PushClip({20, 0, 40, 20});
        painter.Line({0, 0}, {40, 40}, 2, {1, 1, 1, 1});
        const auto clip = painter.List().batches.back().clip;
        Require(clip.x == 20 && clip.y == 10 && clip.width == 20 && clip.height == 10, "Nested clip intersection");
        painter.PopClip(); painter.PopClip();
        painter.RoundedRect({0, 0, 10, 6}, 20, {1, 1, 1, 1});
        Require(painter.List().commands.back().parameters[1] == 3 && painter.List().batches.size() == 3, "Radius clamp or painter order");
        painter.PushClip({300, 0, 10, 10});
        painter.Circle({20, 20}, 5, {1, 1, 1, 1});
        Require(painter.List().commands.size() == 4, "Empty clip must suppress draws");
        painter.PopClip();
        bool rejected = false;
        try { painter.PopClip(); } catch (const std::logic_error&) { rejected = true; }
        Require(rejected, "Root clip underflow");
        rejected = false;
        try { painter.Circle({0, 0}, std::numeric_limits<float>::quiet_NaN(), {}); }
        catch (const std::invalid_argument&) { rejected = true; }
        Require(rejected, "NaN must be rejected");
        const auto before = painter.List().commands.size();
        painter.Line({0, 0}, {20, 20}, 0, {1, 1, 1, 1});
        Require(painter.List().commands.size() == before, "Zero-width diagonal line must not produce a hairline");
        Painter capacity(100, 100);
        for (uint32_t i = 0; i < MaxCommands; ++i) capacity.FillRect({0, 0, 1, 1}, {});
        rejected = false;
        try { capacity.FillRect({0, 0, 1, 1}, {}); } catch (const std::length_error&) { rejected = true; }
        Require(rejected && capacity.List().commands.size() == MaxCommands, "Bounded upload capacity");
        std::cout << "Renderer2D batching, clip stack, ordering, geometry validation and capacity passed.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
