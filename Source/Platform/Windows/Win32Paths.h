#pragma once
#include <expected>
#include <filesystem>
#include <string>
namespace ih::platform {
std::expected<std::filesystem::path, std::string> UserDataDirectory();
std::expected<std::filesystem::path, std::string> ExecutableDirectory();
}
