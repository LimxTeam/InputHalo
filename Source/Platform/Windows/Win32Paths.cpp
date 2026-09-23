/*******************************************************************************
 * 文件: Win32Paths.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   实现路径解析：获取可执行文件目录与用户数据目录（LocalAppData/InputHalo）。
 *   不涉及复杂算法；单次系统调用。
 *   空间-时间权衡：使用栈上缓冲避免堆分配。
 *
 * 设计哲学:
 *   封装 Windows 路径约定，为配置文件与着色器加载提供跨环境路径。
 *
 * 技术特性:
 *   - 可执行目录：GetModuleFileNameW 获取当前 exe 所在目录
 *   - 用户数据目录：SHGetKnownFolderPath 定位 LocalAppData 并拼接 InputHalo 子目录
 *   - 内存安全：用 unique_ptr + CoTaskMemFree 自动释放 Shell 分配
 *
 * 依赖关系:
 *   外部依赖：Win32 API（GetModuleFileNameW、SHGetKnownFolderPath）、Shell32
 *
 * 注意事项:
 *   用户数据目录不存在时由调用方负责创建
 *
 ******************************************************************************/
#include "Platform/Windows/Win32Paths.h"
#include <Windows.h>
#include <ShlObj.h>
#include <memory>

// Win32 路径管理命名空间
namespace ih::platform {
// 获取可执行文件所在目录
// 返回 exe 父目录路径；失败返回错误
std::expected<std::filesystem::path, std::string> ExecutableDirectory() {
    wchar_t path[32768]{};
    // 获取当前进程模块完整路径（栈上 32K 缓冲）
    const auto length = GetModuleFileNameW(nullptr, path, 32768);
    // 长度为 0 或被截断（等于缓冲大小）视为失败
    if (!length || length == 32768) return std::unexpected("Cannot resolve executable directory");
    // 取父目录即 exe 所在目录
    return std::filesystem::path(path).parent_path();
}
// 获取用户数据目录：%LocalAppData%/InputHalo
// 用于存放配置、预设等用户数据
std::expected<std::filesystem::path, std::string> UserDataDirectory() {
    PWSTR raw = nullptr;
    // 调用 Shell API 定位 LocalAppData 已知文件夹
    const auto status = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw);
    // 用 unique_ptr 绑定 CoTaskMemFree，自动释放 Shell 内存
    std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owner(raw, &CoTaskMemFree);
    if (FAILED(status)) return std::unexpected("Cannot resolve LocalAppData");
    // 拼接 InputHalo 子目录作为应用数据根目录
    return std::filesystem::path(raw) / L"InputHalo";
}
}
