#include "Core/Logging/Logging.h"
#include "Platform/Windows/Win32Dpi.h"
#include "Platform/Windows/Win32Window.h"
#include "Platform/Windows/Win32Paths.h"
#include "Platform/Windows/Win32Monitor.h"
#include "RHI/Vulkan/VulkanSwapchain.h"
#include "Overlay/OverlayManager.h"
#include <iostream>
#include <thread>

namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class T> void Check(const T& result) { if (!result) throw std::runtime_error(result.error()); }
}
int main() {
    try {
        Check(ih::platform::InitializeDpi());
        ih::Logging logging(std::filesystem::current_path() / "overlay-smoke-logs");
        ih::platform::Win32Window window;
        Check(window.Create(GetModuleHandleW(nullptr)));
        ih::render::VulkanContext context;
        Check(context.Initialize(GetModuleHandleW(nullptr), window.Handle(), true));
        ih::render::VulkanSwapchain settings(context);
        Check(settings.Initialize());
        window.Show(SW_SHOWNORMAL);
        ih::OverlayManager overlays(context);
        const auto directory = ih::platform::ExecutableDirectory();
        Check(directory);
        Require(!overlays.Initialize(*directory / "MissingOverlayShader.spv"), "Missing shader must fail");
        Require(overlays.TargetCount() == 0, "Failure must clean up overlays");
        const auto drawSettings = [&] {
            const auto size = window.ClientSize();
            Check(settings.Draw(size.cx, size.cy));
        };
        drawSettings();
        const auto monitors = ih::platform::EnumerateMonitors();
        Check(monitors);
        const auto initialize = [&] {
            Check(overlays.Initialize(*directory / "Shaders/Renderer2D.vert.spv"));
            Require(overlays.TargetCount() == monitors->size(), "One target required per monitor");
        };
        const auto settle = [&] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            for (;;) {
                window.PumpMessages();
                auto pending = overlays.Update();
                Check(pending);
                drawSettings();
                if (!*pending) break;
                Require(std::chrono::steady_clock::now() < deadline, "Overlay timed out");
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
        };
        initialize();
        settle();
        Require(overlays.PresentedFrames() == monitors->size(), "First frame missing");
        for (int i = 0; i < 20; ++i) {
            auto idle = overlays.Update();
            Check(idle);
            Require(!*idle && overlays.PresentedFrames() == monitors->size(), "Idle must not submit");
        }
        for (int i = 0; i < 8; ++i) { overlays.RequestRedraw(); settle(); }
        Require(overlays.PresentedFrames() == monitors->size() * 9, "Buffered redraw lost a frame");
        for (int i = 0; i < 3; ++i) {
            overlays.RequestRedraw();
            Check(overlays.Update());
            initialize(); // Rebuild while GPU work may still be pending.
            settle();
        }
        SendMessageW(window.Handle(), WM_DISPLAYCHANGE, 32, MAKELPARAM(1920, 1080));
        Require(window.ConsumeDisplayChange(), "Display change was not recorded");
        initialize();
        settle();
        SendMessageW(window.Handle(), WM_POWERBROADCAST, PBT_APMRESUMEAUTOMATIC, 0);
        Require(window.ConsumeDisplayChange(), "Resume was not recorded");
        initialize();
        settle();
        Require(!overlays.Initialize(*directory / "MissingOverlayShader.spv"), "Reinitialization failure missing");
        Require(overlays.TargetCount() == 0, "Failed rebuild retained windows");
        drawSettings();
        initialize();
        settle();
        overlays.Shutdown();
        settings.Shutdown();
        context.Shutdown();
        Require(context.ValidationWarnings() == 0 && context.ValidationErrors() == 0, "Validation not clean");
        std::cout << "Overlay smoke passed: " << monitors->size()
            << " monitors; idle, buffered redraw, pending rebuild, display/resume messages, missing shader recovery; validation clean.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

