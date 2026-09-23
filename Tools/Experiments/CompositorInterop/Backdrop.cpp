#include "Core/Logging/Logging.h"
#include "Platform/Windows/Win32Dpi.h"
#include "Platform/Windows/Win32Window.h"
#include "RHI/Vulkan/VulkanContext.h"
#include "RHI/Vulkan/VulkanSwapchain.h"
#include <filesystem>
#include <stdexcept>

namespace {
WNDPROC previousProc{};
unsigned clicks{};
BOOL CALLBACK MoveToSecondary(HMONITOR monitor, HDC, LPRECT, LPARAM user) {
    MONITORINFO info{sizeof(MONITORINFO)};
    if (GetMonitorInfoW(monitor, &info) && !(info.dwFlags & MONITORINFOF_PRIMARY)) {
        SetWindowPos(reinterpret_cast<HWND>(user), nullptr, info.rcMonitor.left + 400, info.rcMonitor.top + 220,
            1120, 720, SWP_NOZORDER);
        return FALSE;
    }
    return TRUE;
}
LRESULT CALLBACK CountClicks(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_KEYDOWN && wParam == VK_F2) {
        EnumDisplayMonitors(nullptr, nullptr, MoveToSecondary, reinterpret_cast<LPARAM>(window));
        return 0;
    }
    if (message == WM_LBUTTONDOWN) {
        ++clicks;
        const auto title = L"InputHalo Interop Test Target - clicks: " + std::to_wstring(clicks);
        SetWindowTextW(window, title.c_str());
    }
    return CallWindowProcW(previousProc, window, message, wParam, lParam);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    try {
        if (!ih::platform::InitializeDpi()) return 1;
        ih::Logging logging(std::filesystem::temp_directory_path() / "InputHaloInteropBackdrop");
        ih::platform::Win32Window window;
        if (!window.Create(instance)) return 1;
        SetWindowTextW(window.Handle(), L"InputHalo Interop Test Target - clicks: 0");
        SetWindowPos(window.Handle(), nullptr, 400, 220, 1120, 720, SWP_NOZORDER);
        previousProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window.Handle(), GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(CountClicks)));
        ih::render::VulkanContext vulkan;
        if (!vulkan.Initialize(instance, window.Handle())) return 1;
        ih::render::VulkanSwapchain swapchain(vulkan);
        if (!swapchain.Initialize()) return 1;
        window.Show(SW_SHOWNORMAL);
        while (!window.CloseRequested()) {
            window.PumpMessages();
            if (window.CloseRequested()) break;
            const auto size = window.ClientSize();
            bool retry = false;
            if (size.cx && size.cy && window.NeedsRedraw()) {
                const auto drawn = swapchain.Draw(static_cast<uint32_t>(size.cx), static_cast<uint32_t>(size.cy));
                if (!drawn) return 1;
                if (*drawn) window.AcknowledgeRedraw(); else retry = true;
            }
            MsgWaitForMultipleObjectsEx(0, nullptr, retry ? 16 : INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
        return 0;
    } catch (...) { return 1; }
}
