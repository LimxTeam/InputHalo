/*******************************************************************************
 * 文件: Win32Fonts.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   枚举系统默认字体路径：定位 Windows Fonts 目录并筛选 Segoe UI、微软雅黑、宋体、Arial 等常用字体。
 *   不涉及复杂算法；遍历固定字体名列表。
 *   空间-时间权衡：仅收集存在的字体文件路径，不加载字体数据。
 *
 * 设计哲学:
 *   直接引用系统已安装字体，不随应用分发；支持中英文渲染回退。
 *
 * 技术特性:
 *   - 字体发现：通过 KNOWNFOLDERID_Fonts 定位系统字体目录
 *   - 回退列表：Segoe UI（英文）、微软雅黑（中文）、宋体（中文回退）、Arial（兜底）
 *   - 存在性检查：仅返回实际存在的字体文件
 *
 * 依赖关系:
 *   外部依赖：Win32 API（SHGetKnownFolderPath）、Shell32、C++ 标准库 <filesystem>
 *
 * 注意事项:
 *   字体不随应用分发，遵循系统字体许可
 *   若所有候选字体都不存在则抛出异常
 *
 ******************************************************************************/
#include "Platform/Windows/Win32Fonts.h"
#include <Windows.h>
#include <ShlObj.h>
#include <stdexcept>
// Win32 字体管理命名空间
namespace ih::platform {
// 获取系统默认字体文件路径列表
// 返回存在的字体路径，供 DirectWrite 渲染器按序回退使用
std::vector<std::filesystem::path> DefaultFontPaths() {
    // 调用 Shell API 获取系统 Fonts 目录路径
    PWSTR value{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Fonts, 0, nullptr, &value))) throw std::runtime_error("Cannot locate Windows fonts");
    // 构造字体目录路径对象
    const std::filesystem::path directory(value);
    // 释放 Shell 分配的内存
    CoTaskMemFree(value);
    std::vector<std::filesystem::path> result;
    // 遍历候选字体文件名，检查是否存在于字体目录
    for (const auto name : {L"segoeui.ttf", L"msyh.ttc", L"simsun.ttc", L"arial.ttf"}) {
        const auto path = directory / name;
        // 仅收集实际存在的常规文件
        if (std::filesystem::is_regular_file(path)) result.push_back(path);
    }
    // 若一个字体都没找到，抛出异常
    if (result.empty()) throw std::runtime_error("No supported system fonts found");
    return result;
}
}
