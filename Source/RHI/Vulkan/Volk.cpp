/*******************************************************************************
 * 文件: Volk.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   Volk（Vulkan 函数加载库）的实现编译单元：定义 VOLK_IMPLEMENTATION 以展开函数指针加载代码。
 *   不涉及复杂算法；仅做版本一致性编译期校验。
 *   将所有 Vulkan 函数指针集中在此 TU 加载，其余文件只声明调用。
 *
 * 设计哲学:
 *   通过 Volk 动态加载 Vulkan 入口，避免链接 vulkan-1.lib 并实现运行时版本协商；
 *   在此 TU 固化 SDK 版本，防止头文件与运行库版本漂移。
 *
 * 技术特性:
 *   - 单一实现点: 全工程仅此处定义 VOLK_IMPLEMENTATION。
 *   - 版本钉死: static_assert 校验 Volk 头版本 == 357，与 SDK 1.4.357.0 对齐。
 *
 * 依赖关系:
 *   外部: Volk（随 Vulkan SDK 1.4.357.0 提供）、Vulkan 1.4
 *
 * 注意事项:
 *   必须使用随 SDK 1.4.357.0 附带的 Volk，否则编译期断言失败。
 *   不要在其他 TU 再次定义 VOLK_IMPLEMENTATION。
 *
 ******************************************************************************/
// 定义 VOLK_IMPLEMENTATION：在此翻译单元展开 Volk 的全部函数指针加载实现。
#define VOLK_IMPLEMENTATION
#include <Volk/volk.h>
// 编译期钉住 Volk 版本，确保与 Vulkan SDK 1.4.357.0 配套。
static_assert(VOLK_HEADER_VERSION == 357, "Use Volk shipped with Vulkan SDK 1.4.357.0");
