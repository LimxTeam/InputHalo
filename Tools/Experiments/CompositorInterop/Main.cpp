#include "InteropTarget.h"
#include "Core/Logging/Logging.h"
#include "Platform/Windows/Win32Dpi.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <chrono>
#include <array>
#include <fstream>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

namespace {
constexpr wchar_t kWindowClass[] = L"InputHalo.CompositorInterop";
constexpr DWORD kStyles = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT |
    WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED;
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_CLOSE:
    case WM_DISPLAYCHANGE: PostQuitMessage(0); return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(window, &paint);
        EndPaint(window, &paint);
        return 0;
    }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
struct WindowClass {
    WindowClass() {
        WNDCLASSEXW definition{sizeof(WNDCLASSEXW)};
        definition.hInstance = GetModuleHandleW(nullptr);
        definition.lpfnWndProc = WindowProc;
        definition.lpszClassName = kWindowClass;
        if (!RegisterClassExW(&definition)) throw std::runtime_error("Register overlay class failed");
    }
    ~WindowClass() { UnregisterClassW(kWindowClass, GetModuleHandleW(nullptr)); }
};
struct Overlay {
    HWND window{};
    RECT bounds{};
    std::array<COLORREF, 3> baseline{};
    std::unique_ptr<ih::experiment::InteropTarget> target;
    ~Overlay() { target.reset(); if (window) DestroyWindow(window); }
};
// Test instrumentation only: sample three desktop pixels, never used for rendering/presentation.
std::array<COLORREF, 3> SampleDesktop(const RECT& bounds) {
    HDC desktop = GetDC(nullptr);
    if (!desktop) throw std::runtime_error("Cannot sample compositor output");
    const int x = (bounds.left + bounds.right) / 2;
    const int y = (bounds.top + bounds.bottom) / 2;
    std::array<COLORREF, 3> colors{GetPixel(desktop, x, y), GetPixel(desktop, x - 100, y), GetPixel(desktop, x + 100, y)};
    ReleaseDC(nullptr, desktop);
    for (const auto color : colors) if (color == CLR_INVALID) throw std::runtime_error("Invalid desktop pixel sample");
    return colors;
}
BOOL CALLBACK MonitorCallback(HMONITOR monitor, HDC, LPRECT, LPARAM user) {
    MONITORINFO info{sizeof(MONITORINFO)};
    if (!GetMonitorInfoW(monitor, &info)) return FALSE;
    try { reinterpret_cast<std::vector<RECT>*>(user)->push_back(info.rcMonitor); return TRUE; }
    catch (...) { return FALSE; }
}
}

int main(int argc, char** argv) {
    std::filesystem::path results;
    try {
        const bool preview = argc > 1 && std::string_view(argv[1]) == "--preview";
        const auto dpi = ih::platform::InitializeDpi();
        if (!dpi) throw std::runtime_error(dpi.error());
        wchar_t executable[32768]{};
        const auto length = GetModuleFileNameW(nullptr, executable, 32768);
        if (!length || length == 32768) throw std::runtime_error("Cannot resolve executable directory");
        const auto directory = std::filesystem::path(executable).parent_path();
        results = directory / "interop-results";
        ih::Logging logging(results);
        { std::ofstream pending(results / "report.json"); pending << R"({"gpu_test_passed":false,"result":"RUNNING"})"; }
        WindowClass windowClass;
        ih::experiment::InteropDevice device;
        device.Initialize(directory / "InteropCircle.spv");
        std::vector<RECT> monitors;
        if (!EnumDisplayMonitors(nullptr, nullptr, MonitorCallback, reinterpret_cast<LPARAM>(&monitors)) || monitors.empty())
            throw std::runtime_error("Monitor enumeration failed");
        nlohmann::json report{{"gpu", device.gpuName}, {"validation_enabled", IH_VULKAN_VALIDATION != 0},
            {"path", "Vulkan compute -> shared D3D11 texture -> GPU copy -> DirectComposition"},
            {"targets", nlohmann::json::array()}, {"visual_acceptance", "requires_desktop_inspection"},
            {"click_through_acceptance", "requires_cross_process_input_test"}};
        std::vector<std::unique_ptr<Overlay>> overlays;
        for (const auto& bounds : monitors) {
            auto overlay = std::make_unique<Overlay>();
            overlay->bounds = bounds;
            overlay->window = CreateWindowExW(kStyles, kWindowClass, L"InputHalo Interop Overlay", WS_POPUP,
                bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (!overlay->window) throw std::runtime_error("Create overlay failed");
            if (!SetLayeredWindowAttributes(overlay->window, 0, 255, LWA_ALPHA)) throw std::runtime_error("Set layered window attributes failed");
            overlay->target = std::make_unique<ih::experiment::InteropTarget>(device);
            const auto width = static_cast<uint32_t>(bounds.right - bounds.left);
            const auto height = static_cast<uint32_t>(bounds.bottom - bounds.top);
            overlay->target->Initialize(overlay->window, width, height);
            overlay->target->Draw(width / 2.0f, height / 2.0f, 48.0f * GetDpiForWindow(overlay->window) / 96.0f, 0.85f, true);
            report["targets"].push_back({{"bounds_px", {bounds.left, bounds.top, bounds.right, bounds.bottom}},
                {"dpi", GetDpiForWindow(overlay->window)}, {"pixels_verified", overlay->target->pixelsVerified},
                {"corner_rgba", overlay->target->cornerPixel}, {"center_rgba", overlay->target->centerPixel},
                {"extended_styles", static_cast<uint32_t>(GetWindowLongPtrW(overlay->window, GWL_EXSTYLE))}});
            overlays.push_back(std::move(overlay));
        }
        const auto foreground = GetForegroundWindow();
        for (const auto& overlay : overlays) overlay->baseline = SampleDesktop(overlay->bounds);
        for (const auto& overlay : overlays) ShowWindow(overlay->window, SW_SHOWNOACTIVATE);
        report["activation_preserved"] = foreground == GetForegroundWindow();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(preview ? 60 : 30);
        unsigned frames = 0;
        bool quit = false;
        while (!quit && (preview || frames < 120) && std::chrono::steady_clock::now() < deadline) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) quit = true;
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (quit) break;
            for (const auto& overlay : overlays) {
                const auto width = static_cast<float>(overlay->bounds.right - overlay->bounds.left);
                const auto height = static_cast<float>(overlay->bounds.bottom - overlay->bounds.top);
                overlay->target->Draw(width / 2, height / 2, 48.0f * GetDpiForWindow(overlay->window) / 96.0f, 0.85f, false);
            }
            ++frames;
            if (frames == 10) {
                for (size_t index = 0; index < overlays.size(); ++index) {
                    const auto& overlay = overlays[index];
                    const auto sampled = SampleDesktop(overlay->bounds);
                    report["targets"][index]["desktop_before_colorref"] = overlay->baseline;
                    report["targets"][index]["desktop_after_colorref"] = sampled;
                    report["targets"][index]["desktop_center_changed"] = sampled[0] != overlay->baseline[0];
                    report["targets"][index]["desktop_outside_circle_unchanged"] = sampled[1] == overlay->baseline[1] && sampled[2] == overlay->baseline[2];
                    spdlog::get("Overlay")->info("Desktop sample monitor {}: before={},{},{}; after={},{},{}", index,
                        overlay->baseline[0], overlay->baseline[1], overlay->baseline[2], sampled[0], sampled[1], sampled[2]);
                }
            }
            MsgWaitForMultipleObjectsEx(0, nullptr, 16, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
        overlays.clear();
        device.Shutdown();
        report["frames_per_monitor"] = frames + 1;
        report["validation_errors"] = device.errors.load();
        report["validation_warnings"] = device.warnings.load();
        report["gpu_test_passed"] = frames >= 120 && device.errors == 0 && device.warnings == 0;
        std::ofstream output(results / "report.json");
        output.exceptions(std::ios::failbit | std::ios::badbit);
        output << report.dump(2) << '\n';
        output.close();
        std::cout << "Interop: " << overlays.size() << " remaining windows; " << frames + 1
            << " frames per monitor; validation errors=" << device.errors << ", warnings=" << device.warnings << '\n';
        return report["gpu_test_passed"].get<bool>() ? 0 : 1;
    } catch (const std::exception& error) {
        if (!results.empty()) {
            try { std::ofstream failure(results / "report.json"); failure << nlohmann::json{{"gpu_test_passed", false}, {"error", error.what()}}.dump(2); }
            catch (...) {}
        }
        std::cerr << error.what() << '\n';
        return 1;
    }
}
