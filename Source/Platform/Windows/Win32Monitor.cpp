#include "Platform/Windows/Win32Monitor.h"
namespace ih::platform {
namespace {
BOOL CALLBACK Collect(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    MONITORINFO info{sizeof(MONITORINFO)};
    if (!GetMonitorInfoW(monitor, &info)) return FALSE;
    try {
        reinterpret_cast<std::vector<MonitorInfo>*>(data)->push_back({monitor, info.rcMonitor});
        return TRUE;
    } catch (...) { return FALSE; }
}
}
std::expected<std::vector<MonitorInfo>, std::string> EnumerateMonitors() {
    std::vector<MonitorInfo> monitors;
    if (!EnumDisplayMonitors(nullptr, nullptr, Collect, reinterpret_cast<LPARAM>(&monitors)) || monitors.empty())
        return std::unexpected("Cannot enumerate display monitors");
    return monitors;
}
}
