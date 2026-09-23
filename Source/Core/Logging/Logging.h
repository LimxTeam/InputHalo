#pragma once
#include <filesystem>

namespace ih {
class Logging {
public:
    explicit Logging(const std::filesystem::path& directory);
    ~Logging();
    Logging(const Logging&) = delete;
    Logging& operator=(const Logging&) = delete;
};
}
