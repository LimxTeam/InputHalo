#pragma once
#include <chrono>
namespace ih {
inline double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
}
