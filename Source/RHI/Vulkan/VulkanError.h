/*******************************************************************************
 * 文件: VulkanError.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   Vulkan 错误检查工具：把 VkResult 转为 C++ 异常，统一错误上报口径。
 *   不涉及复杂算法；单次比较 + 字符串拼接。
 *   无状态内联函数，零额外开销。
 *
 * 设计哲学:
 *   所有 Vulkan 调用后经 CheckVk 检查，失败即抛 runtime_error 并附带操作名与 VkResult，
 *   使错误来源可定位；对 DEVICE_LOST 额外标注，便于上层识别需重建设备的致命错误。
 *
 * 技术特性:
 *   - 统一错误语义: 任何非 VK_SUCCESS 都抛异常。
 *   - 上下文信息: 异常消息包含调用方传入的操作名。
 *
 * 依赖关系:
 *   内部: Limx::ih::render
 *   外部: Vulkan(Volk)、<stdexcept>/<string>
 *
 * 注意事项:
 *   调用方不应在热路径捕获该异常；它用于启动与资源创建期的硬失败。
 *   线程安全：纯函数，可任意线程调用。
 *
 ******************************************************************************/
#pragma once
#include <Volk/volk.h>
#include <stdexcept>
#include <string>

namespace ih::render {
// CheckVk: VkResult != VK_SUCCESS 即抛 runtime_error，附带操作名与结果码。
inline void CheckVk(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed (VkResult " +
            std::to_string(result) + (result == VK_ERROR_DEVICE_LOST ? ", device lost)" : ")"));
}
}
