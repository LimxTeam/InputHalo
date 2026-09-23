#include "Core/Logging/Logging.h"
#include "Platform/Windows/Win32Dpi.h"
#include "Platform/Windows/Win32Window.h"
#include "RHI/Vulkan/VulkanContext.h"
#include "RHI/Vulkan/VulkanError.h"
#include "AlphaPolicy.h"
#include <dwmapi.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using ih::render::CheckVk;
constexpr wchar_t kClassName[] = L"InputHalo.OverlayProbe";
constexpr DWORD kOverlayStyles = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT;

const char* SelectAlpha(VkCompositeAlphaFlagsKHR supported) {
    const auto mode = ih::experiment::SelectOverlayAlpha(supported);
    if (mode == VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) return "PRE_MULTIPLIED";
    if (mode == VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) return "POST_MULTIPLIED";
    if (mode == VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) return "INHERIT_REQUIRES_NATIVE_VERIFICATION";
    return "NONE";
}

struct Monitor {
    RECT bounds{};
    bool primary{};
};
BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM user) {
    MONITORINFO info{sizeof(MONITORINFO)};
    if (!GetMonitorInfoW(monitor, &info)) return FALSE;
    try {
        reinterpret_cast<std::vector<Monitor>*>(user)->push_back({info.rcMonitor, (info.dwFlags & MONITORINFOF_PRIMARY) != 0});
        return TRUE;
    } catch (...) { return FALSE; }
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
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
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WindowClass() {
        WNDCLASSEXW definition{sizeof(WNDCLASSEXW)};
        definition.lpfnWndProc = WindowProc;
        definition.hInstance = instance;
        definition.lpszClassName = kClassName;
        if (!RegisterClassExW(&definition)) throw std::runtime_error("Cannot register probe window class");
    }
    ~WindowClass() { UnregisterClassW(kClassName, instance); }
};

struct ProbeTarget {
    HWND window{};
    VkInstance instance{};
    VkSurfaceKHR surface{};
    ProbeTarget() = default;
    ProbeTarget(const ProbeTarget&) = delete;
    ProbeTarget& operator=(const ProbeTarget&) = delete;
    ~ProbeTarget() {
        if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
        if (window) DestroyWindow(window);
    }
};

enum class DwmMode { None, ExtendedFrame, Blur, ExtendedFrameAndBlur };
struct Variant { const char* name; DWORD additionalStyles; DwmMode dwm; };
constexpr std::array kVariants{
    Variant{"plain_popup", 0, DwmMode::None},
    Variant{"extended_frame", 0, DwmMode::ExtendedFrame},
    Variant{"blur_region", 0, DwmMode::Blur},
    Variant{"extended_frame_and_blur", 0, DwmMode::ExtendedFrameAndBlur},
    Variant{"layered_blur_region", WS_EX_LAYERED, DwmMode::Blur},
    Variant{"no_redirection_blur_region", WS_EX_NOREDIRECTIONBITMAP, DwmMode::Blur}
};

nlohmann::json Probe(ih::render::VulkanContext& context, const Monitor& monitor, const Variant& variant,
    const std::vector<VkPhysicalDevice>& devices) {
    ProbeTarget target;
    target.window = CreateWindowExW(kOverlayStyles | variant.additionalStyles, kClassName, L"InputHalo Overlay Probe",
        WS_POPUP, monitor.bounds.left, monitor.bounds.top, monitor.bounds.right - monitor.bounds.left,
        monitor.bounds.bottom - monitor.bounds.top, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!target.window) throw std::runtime_error("Cannot create hidden overlay probe");
    nlohmann::json row{
        {"variant", variant.name}, {"visible", false}, {"extended_styles", kOverlayStyles | variant.additionalStyles},
        {"dpi", GetDpiForWindow(target.window)}, {"devices", nlohmann::json::array()}
    };
    if (variant.dwm == DwmMode::ExtendedFrame || variant.dwm == DwmMode::ExtendedFrameAndBlur) {
        const MARGINS margins{-1, -1, -1, -1};
        row["extend_frame_hresult"] = static_cast<int32_t>(DwmExtendFrameIntoClientArea(target.window, &margins));
    }
    if (variant.dwm == DwmMode::Blur || variant.dwm == DwmMode::ExtendedFrameAndBlur) {
        const auto region = CreateRectRgn(0, 0, -1, -1);
        if (!region) throw std::runtime_error("Cannot create DWM region");
        DWM_BLURBEHIND blur{};
        blur.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
        blur.fEnable = TRUE;
        blur.hRgnBlur = region;
        const auto status = DwmEnableBlurBehindWindow(target.window, &blur);
        DeleteObject(region);
        row["blur_hresult"] = static_cast<int32_t>(status);
    }
    // No color key, global alpha, GDI drawing or swapchain is used by this capability probe.
    VkWin32SurfaceCreateInfoKHR create{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    create.hinstance = GetModuleHandleW(nullptr);
    create.hwnd = target.window;
    target.instance = context.Instance();
    CheckVk(vkCreateWin32SurfaceKHR(context.Instance(), &create, nullptr, &target.surface), "Probe surface");
    for (const auto gpu : devices) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(gpu, &properties);
        VkSurfaceCapabilitiesKHR capabilities{};
        CheckVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, target.surface, &capabilities), "Probe capabilities");
        uint32_t familyCount{};
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &familyCount, families.data());
        bool graphicsPresent = false;
        for (uint32_t index = 0; index < familyCount; ++index) {
            VkBool32 present{};
            CheckVk(vkGetPhysicalDeviceSurfaceSupportKHR(gpu, index, target.surface, &present), "Probe queue support");
            graphicsPresent |= present && (families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT);
        }
        const auto* alpha = SelectAlpha(capabilities.supportedCompositeAlpha);
        row["devices"].push_back({
            {"name", properties.deviceName}, {"vendor_id", properties.vendorID}, {"device_id", properties.deviceID},
            {"api_version", properties.apiVersion}, {"driver_version", properties.driverVersion},
            {"graphics_present", graphicsPresent}, {"supported_composite_alpha", capabilities.supportedCompositeAlpha},
            {"selected_alpha", alpha}, {"current_extent", {capabilities.currentExtent.width, capabilities.currentExtent.height}}
        });
        spdlog::get("Overlay")->info("{}: GPU={}, alpha mask={}, selection={}, graphics/present={}",
            variant.name, properties.deviceName, capabilities.supportedCompositeAlpha, alpha, graphicsPresent);
    }
    return row;
}
}

int main() {
    try {
        auto dpi = ih::platform::InitializeDpi();
        if (!dpi) throw std::runtime_error(dpi.error());
        const auto outputDirectory = std::filesystem::current_path() / "overlay-probe-results";
        ih::Logging logging(outputDirectory);
        WindowClass windowClass;
        ih::platform::Win32Window settings;
        auto created = settings.Create(GetModuleHandleW(nullptr));
        if (!created) throw std::runtime_error(created.error());
        ih::render::VulkanContext context;
        auto initialized = context.Initialize(GetModuleHandleW(nullptr), settings.Handle());
        if (!initialized) throw std::runtime_error(initialized.error());
        std::vector<Monitor> monitors;
        if (!EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&monitors)) || monitors.empty())
            throw std::runtime_error("Cannot enumerate monitors");
        uint32_t count{};
        CheckVk(vkEnumeratePhysicalDevices(context.Instance(), &count, nullptr), "Enumerate probe devices");
        std::vector<VkPhysicalDevice> devices(count);
        CheckVk(vkEnumeratePhysicalDevices(context.Instance(), &count, devices.data()), "Enumerate probe devices");
        BOOL composition{};
        const auto dwmStatus = DwmIsCompositionEnabled(&composition);
        nlohmann::json report{
            {"stage", "capability_probe_only"}, {"sdk", "1.4.357.0"}, {"validation_enabled", IH_VULKAN_VALIDATION != 0},
            {"dwm_hresult", static_cast<int32_t>(dwmStatus)}, {"dwm_composition", composition != FALSE},
            {"monitors", nlohmann::json::array()}, {"visual_acceptance_passed", false}
        };
        bool candidate = false;
        for (const auto& monitor : monitors) {
            nlohmann::json monitorReport{
                {"bounds_px", {monitor.bounds.left, monitor.bounds.top, monitor.bounds.right, monitor.bounds.bottom}},
                {"primary", monitor.primary}, {"variants", nlohmann::json::array()}
            };
            for (const auto& variant : kVariants) {
                auto row = Probe(context, monitor, variant, devices);
                for (const auto& gpu : row["devices"])
                    candidate |= gpu["graphics_present"].get<bool>() && gpu["selected_alpha"] != "NONE";
                monitorReport["variants"].push_back(std::move(row));
            }
            report["monitors"].push_back(std::move(monitorReport));
        }
        context.Shutdown();
        report["validation_errors"] = context.ValidationErrors();
        report["validation_warnings"] = context.ValidationWarnings();
        report["transparent_mode_candidate"] = candidate;
        report["result"] = candidate ? "CANDIDATE_REQUIRES_RENDERING_TEST" : "OVERLAY_COMPATIBILITY_ERROR";
        std::ofstream output(outputDirectory / "report.json");
        output.exceptions(std::ios::failbit | std::ios::badbit);
        output << report.dump(2) << '\n';
        output.close();
        std::cout << report["result"].get<std::string>() << ": " << monitors.size() << " monitors, "
            << devices.size() << " Vulkan devices, " << kVariants.size() << " window variants.\n"
            << "Report: " << (outputDirectory / "report.json").string() << '\n';
        if (context.ValidationErrors() || context.ValidationWarnings()) return 1;
        return candidate ? 0 : 2; // 0 indicates a candidate, NOT visual acceptance.
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
