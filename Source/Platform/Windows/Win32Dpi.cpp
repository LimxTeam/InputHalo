#include "Platform/Windows/Win32Dpi.h"
#include <Windows.h>
namespace ih::platform {
std::expected<void, std::string> InitializeDpi() {
    if (SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return {};
    if (AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(),DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return {};
    return std::unexpected("Cannot enable per-monitor V2 DPI: " + std::to_string(GetLastError()));
}
}
