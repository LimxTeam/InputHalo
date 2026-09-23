/*******************************************************************************
 * 文件: Logging.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   日志系统头文件声明。Logging 类封装 spdlog 初始化与关闭，
 *   采用 RAII 模式：构造时启动日志，析构时关闭日志。
 *   算法复杂度：不涉及复杂算法。
 *   空间-时间权衡：不涉及。
 *
 * 设计哲学:
 *   RAII 生命周期管理：Logging 对象在作用域内存在期间日志系统有效，
 *   离开作用域自动关闭。禁止拷贝，防止意外重复初始化/关闭。
 *
 * 技术特性:
 *   - RAII: 构造即初始化，析构即关闭。
 *   - 不可拷贝: 禁止拷贝构造与赋值，确保日志系统唯一实例。
 *
 * 依赖关系:
 *   外部: <filesystem>
 *
 * 注意事项:
 *   - 必须在任何日志输出之前构造 Logging 对象。
 *
 ******************************************************************************/
// 头文件保护
#pragma once
// 文件系统路径
#include <filesystem>

// ih 命名空间
namespace ih {
// Logging 类：spdlog 日志系统的 RAII 封装
class Logging {
// --- 公共接口 ---
public:
// 构造：初始化日志系统，参数为日志目录路径
    explicit Logging(const std::filesystem::path& directory);
// 析构：关闭日志系统
    ~Logging();
// 禁止拷贝构造
    Logging(const Logging&) = delete;
// 禁止拷贝赋值
    Logging& operator=(const Logging&) = delete;
};
}
