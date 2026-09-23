#pragma once
#include <Windows.h>
#include <expected>
#include <string>
#include <vector>
namespace ih::platform {
struct MonitorInfo { HMONITOR handle{}; RECT bounds{}; };
std::expected<std::vector<MonitorInfo>, std::string> EnumerateMonitors();
}
