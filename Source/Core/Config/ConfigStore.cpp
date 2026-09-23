/*******************************************************************************
 * 文件: ConfigStore.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   配置存储的实现文件，提供 LoadConfig 与 SaveConfig 两个函数。
 *   LoadConfig 从 JSON 文件加载配置；若文件损坏则自动备份为 .invalid 后缀并回退默认值。
 *   SaveConfig 采用原子写入策略：先写入临时文件，再用 MoveFileExW 原子替换目标文件，
 *   确保断电或崩溃时不会损坏配置文件。
 *   算法复杂度：O(N)，N 为配置文件大小；JSON 解析为线性扫描。
 *   空间-时间权衡：原子写入需要额外磁盘空间（临时文件），换取配置文件安全性。
 *
 * 设计哲学:
 *   配置损坏不崩溃：JSON 解析失败时自动备份坏文件并使用默认配置，
 *   用户可手动恢复。写入采用临时文件 + 原子移动模式，避免写入中途崩溃导致配置丢失。
 *   使用 std::expected 而非异常返回错误，让调用方显式处理。
 *
 * 技术特性:
 *   - 原子写入: 临时文件 + MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)。
 *   - 损坏自动备份: 解析失败时重命名为 .invalid（已存在则递增序号）。
 *   - 目录自动创建: 保存时自动创建父目录。
 *
 * 依赖关系:
 *   内部: ih::SettingsModel、ih::ConfigStore
 *   外部: Win32 API (MoveFileExW, GetLastError)、nlohmann::json、std::filesystem
 *
 * 注意事项:
 *   - Windows 平台专用（MoveFileExW）。
 *   - WRITE_THROUGH 标志确保数据立即写入磁盘缓存。
 *
 ******************************************************************************/
// ConfigStore 头文件声明
#include "Core/Config/ConfigStore.h"
// Windows API：MoveFileExW
#include <Windows.h>
// 文件流：ifstream/ofstream
#include <fstream>

// ih 命名空间
namespace ih {
// ========================================================================
// LoadConfig — 从 JSON 文件加载配置
// 参数: path — 配置文件路径
// 返回: ConfigLoadResult（含设置与警告）或错误字符串
// ========================================================================
std::expected<ConfigLoadResult, std::string> LoadConfig(const std::filesystem::path& path) {
// 外层 try-catch 捕获文件系统异常
    try {
// 配置文件不存在时直接返回默认配置
        if (!std::filesystem::exists(path)) return ConfigLoadResult{SettingsModel::Defaults(), {}};
// 打开配置文件
        std::ifstream input(path);
// 文件打开失败
        if (!input) return std::unexpected("Cannot open settings file");
// 解析 JSON
        nlohmann::json source;
// 内层 try-catch 专门捕获 JSON 解析错误
        try {
// 解析 JSON 到 nlohmann::json 对象
            input >> source;
// 解析成功：转换为 SettingsModel
            return ConfigLoadResult{SettingsModel::FromJson(source), {}};
// JSON 解析失败：备份坏文件并回退默认
        } catch (const std::exception&) {
// 关闭文件流
            input.close();
// 构造备份路径（.invalid 后缀）
            auto backup = path;
// 备份后缀
            backup += L".invalid";
// 若备份文件已存在，递增序号直到找到可用名称
            for (unsigned index = 1; std::filesystem::exists(backup); ++index) {
// 重置备份路径
                backup = path;
// 追加序号后缀
                backup += L".invalid." + std::to_wstring(index);
// 重命名坏文件为备份
            }
// 返回默认配置并附带警告信息
            std::filesystem::rename(path, backup);
            return ConfigLoadResult{SettingsModel::Defaults(), "Invalid or unsupported settings backed up; defaults restored"};
// 外层异常：文件系统错误等
        }
// 返回错误信息
    } catch (const std::exception& error) {
        return std::unexpected(error.what());
    }
}

// ========================================================================
// SaveConfig — 原子保存配置到 JSON 文件
// 参数: path — 目标文件路径; settings — 要保存的设置模型
// 返回: 成功返回 void，失败返回错误字符串
// ========================================================================
std::expected<void, std::string> SaveConfig(const std::filesystem::path& path, const SettingsModel& settings) {
// try-catch 捕获文件 I/O 异常
    try {
// 自动创建父目录
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
// 构造临时文件路径
        auto temporary = path;
// 追加 .tmp 后缀
        temporary += L".tmp";
// 写入临时文件的作用域块
        {
// 打开临时文件（截断模式）
            std::ofstream output(temporary, std::ios::trunc);
// 设置异常抛出：failbit 和 badbit 时抛异常
            output.exceptions(std::ios::failbit | std::ios::badbit);
// 写入 JSON（缩进 2 空格美化）
            output << settings.values.dump(2) << '\n';
// 关闭文件
            output.close();
        }
// 原子替换：用临时文件覆盖目标文件，WRITE_THROUGH 确保刷盘
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
// 替换失败：返回 Windows 错误码
            return std::unexpected("Cannot replace settings file: " + std::to_string(GetLastError()));
// 成功
        return {};
    } catch (const std::exception& error) {
        return std::unexpected(error.what());
    }
}
}
