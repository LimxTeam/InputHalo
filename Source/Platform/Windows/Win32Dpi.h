#pragma once
#include <expected>
#include <string>
namespace ih::platform {
std::expected<void, std::string> InitializeDpi();
}
