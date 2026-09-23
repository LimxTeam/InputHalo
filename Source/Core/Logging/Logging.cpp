/*******************************************************************************
 * 文件: Logging.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   日志系统实现文件。Logging 构造函数创建旋转文件日志 sink（5MB 单文件，保留 2 个备份），
 *   并注册 6 个命名日志通道（App/Platform/Vulkan/Overlay/Input/Config），
 *   所有通道共享同一 sink。析构时关闭整个 spdlog 日志系统。
 *   算法复杂度：O(1) 初始化，日志写入 O(1)。
 *   空间-时间权衡：旋转文件限制磁盘占用，多通道共享 sink 避免重复 I/O。
 *
 * 设计哲学:
 *   按子系统分通道（App/Platform/Vulkan 等），便于日志过滤与问题定位。
 *   所有通道共享同一个旋转文件 sink，减少文件句柄占用。
 *   flush_on(info) 确保 info 级别及以上日志立即刷盘，避免崩溃时丢失。
 *
 * 技术特性:
 *   - 旋转文件: 5MB 单文件，最多 2 个备份，防止日志无限增长。
 *   - 多通道共享 sink: 6 个 logger 写入同一文件，格式统一。
 *   - 自动刷盘: info 级别及以上立即刷新到磁盘。
 *
 * 依赖关系:
 *   内部: ih::Logging
 *   外部: spdlog (rotating_file_sink_mt)、std::filesystem
 *
 * 注意事项:
 *   - 多线程安全 sink（mt 后缀）。
 *   - 日志格式：[时间] [通道] [级别] 消息。
 *
 ******************************************************************************/
// Logging 头文件声明
#include "Core/Logging/Logging.h"
// spdlog 核心库
#include <spdlog/spdlog.h>
// spdlog 旋转文件 sink
#include <spdlog/sinks/rotating_file_sink.h>

// ih 命名空间
namespace ih {
// ========================================================================
// Logging 构造函数：初始化日志系统
// 参数: directory — 日志文件目录
// ========================================================================
Logging::Logging(const std::filesystem::path& directory) {
// 创建日志目录（如不存在）
    std::filesystem::create_directories(directory);
// 创建旋转文件 sink：5MB 单文件，保留 2 个备份
    auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        (directory / "inputhalo.log").native(), 5 * 1024 * 1024, 2);
// 为每个子系统通道创建 logger，共享同一 sink
    for (const auto* channel : {"App", "Platform", "Vulkan", "Overlay", "Input", "Config"}) {
// 创建 logger 实例
        auto logger = std::make_shared<spdlog::logger>(channel, sink);
// 设置日志格式：[日期 时间.毫秒] [通道] [级别] 消息
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
// info 级别及以上立即刷盘
        logger->flush_on(spdlog::level::info);
// 注册到 spdlog 全局注册表，之后可通过 spdlog::get(通道名) 获取
        spdlog::register_logger(logger);
    }
}
// 析构：关闭并清理整个 spdlog 日志系统
Logging::~Logging() { spdlog::shutdown(); }
}
