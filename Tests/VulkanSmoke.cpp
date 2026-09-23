#include "Core/Logging/Logging.h"
#include "Platform/Windows/Win32Dpi.h"
#include "Platform/Windows/Win32Window.h"
#include "RHI/Vulkan/VulkanContext.h"
#include "RHI/Vulkan/VulkanSwapchain.h"
#include "Platform/Windows/Win32Fonts.h"
#include "Platform/Windows/Win32Paths.h"
#include "Text/TextEngine.h"
#include "Render2D/Painter.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void DrawFrames(ih::platform::Win32Window& window, ih::render::VulkanSwapchain& swapchain, unsigned count) {
    const auto target = swapchain.PresentedFrames() + count;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (swapchain.PresentedFrames() < target) {
        window.PumpMessages();
        const auto size = window.ClientSize();
        const auto drawn = swapchain.Draw(static_cast<uint32_t>(size.cx), static_cast<uint32_t>(size.cy));
        if (!drawn) throw std::runtime_error(drawn.error());
        Require(std::chrono::steady_clock::now() < deadline, "Rendering timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    const auto size = window.ClientSize();
    Require(swapchain.Extent().width == static_cast<uint32_t>(size.cx) &&
        swapchain.Extent().height == static_cast<uint32_t>(size.cy), "Swapchain extent does not match client pixels");
}
}
int main() {
    try {
        const auto dpi = ih::platform::InitializeDpi();
        if (!dpi) throw std::runtime_error(dpi.error());
        ih::Logging logging(std::filesystem::current_path() / "vulkan-smoke-logs");
        ih::platform::Win32Window window;
        const auto created = window.Create(GetModuleHandleW(nullptr));
        if (!created) throw std::runtime_error(created.error());
        ih::render::VulkanContext context;
        const auto initialized = context.Initialize(GetModuleHandleW(nullptr), window.Handle());
        if (!initialized) throw std::runtime_error(initialized.error());
        ih::render::VulkanSwapchain swapchain(context);
        const auto ready = swapchain.Initialize();
        if (!ready) throw std::runtime_error(ready.error());
        ih::text::TextEngine text;
        for (const auto& font : ih::platform::DefaultFontPaths()) text.AddFont(font);
        const auto executable = ih::platform::ExecutableDirectory();
        if (!executable) throw std::runtime_error(executable.error());
        const auto content = [&](unsigned cycle) {
            const float scales[]{1, 1.25f, 1.5f, 2};
            const float scale = scales[cycle % 4];
            ih::render2d::Painter painter(600, 400);
            const auto layout = text.Shape("InputHalo 输入光晕 " + std::to_string(cycle), 24, scale);
            Require(layout.missingGlyphs == 0, "Missing smoke-test glyph");
            painter.Text(layout, {20, 20}, {1, 1, 1, 1});
            swapchain.SetContent(*executable / "Shaders/Renderer2D.vert.spv", painter.List(), scale);
        };
        content(0);
        window.Show(SW_SHOWNORMAL);
        DrawFrames(window, swapchain, 12);
        for (unsigned cycle = 0; cycle < 12; ++cycle) {
            content(cycle + 1); // Replace in-flight atlas snapshots; frame fences protect both copies.
            Require(SetWindowPos(window.Handle(), nullptr, 0, 0, 640 + static_cast<int>(cycle) * 31,
                420 + static_cast<int>(cycle) * 17, SWP_NOMOVE | SWP_NOZORDER) != FALSE, "Resize failed");
            DrawFrames(window, swapchain, 4);
            window.Show(SW_MINIMIZE);
            window.PumpMessages();
            const auto minimized = window.ClientSize();
            Require(minimized.cx == 0 && minimized.cy == 0, "Minimized extent should be zero");
            const auto before = swapchain.PresentedFrames();
            const auto skipped = swapchain.Draw(0, 0);
            Require(skipped.has_value() && !*skipped && swapchain.PresentedFrames() == before, "Minimize must skip GPU submission");
            window.Show(SW_RESTORE);
            DrawFrames(window, swapchain, 4);
        }
        window.Show(SW_MAXIMIZE);
        DrawFrames(window, swapchain, 6);
        window.Show(SW_RESTORE);
        DrawFrames(window, swapchain, 6);
        PostMessageW(window.Handle(), WM_CLOSE, 0, 0);
        window.PumpMessages();
        Require(window.CloseRequested() && IsWindow(window.Handle()), "Close must preserve HWND until Vulkan shutdown");
        const auto presented = swapchain.PresentedFrames();
        swapchain.Shutdown();
        context.Shutdown();
        Require(context.ValidationErrors() == 0 && context.ValidationWarnings() == 0, "Vulkan validation was not clean");
        std::cout << "Vulkan smoke passed: " << presented << " text frames, 12 resize/minimize/restore and atlas/DPI updates, maximize/restore, clean shutdown.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
