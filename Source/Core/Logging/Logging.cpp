#include "Core/Logging/Logging.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>

namespace ih {
Logging::Logging(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        (directory / "inputhalo.log").native(), 5 * 1024 * 1024, 2);
    for (const auto* channel : {"App", "Platform", "Vulkan", "Overlay", "Input", "Config"}) {
        auto logger = std::make_shared<spdlog::logger>(channel, sink);
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
        logger->flush_on(spdlog::level::info);
        spdlog::register_logger(logger);
    }
}
Logging::~Logging() { spdlog::shutdown(); }
}
