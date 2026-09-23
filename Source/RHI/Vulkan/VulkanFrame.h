/*******************************************************************************
 * 文件: VulkanFrame.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   逐帧 GPU 资源聚合：命令池、命令缓冲、获取信号量与完成 fence。
 *   不涉及复杂算法；纯数据结构。
 *   以 kFramesInFlight=2 做双缓冲，在 GPU 忙时允许 CPU 提前准备下一帧。
 *
 * 设计哲学:
 *   把与"一个在飞帧"相关的同步对象打包，配合双缓冲实现 CPU/GPU 并行；
 *   finished fence 同时用于 CPU 等待该帧资源可复用。
 *
 * 技术特性:
 *   - 双缓冲: kFramesInFlight=2，平衡延迟与吞吐。
 *   - 同步原语: acquired 信号量等待可用图像，finished fence 标记整帧完成。
 *
 * 依赖关系:
 *   内部: Limx::ih::render
 *   外部: Vulkan(Volk)
 *
 * 注意事项:
 *   帧资源的复用由 finished fence 守护，上传/重建前必须等待或轮询它。
 *
 ******************************************************************************/
#pragma once
#include <Volk/volk.h>
namespace ih::render {
// VulkanFrame: 一个在飞帧所需的命令与同步对象集合。
struct VulkanFrame {
    VkCommandPool pool{};
    VkCommandBuffer command{};
    VkSemaphore acquired{};
    VkFence finished{};
};
// kFramesInFlight: 帧缓冲数=2（双缓冲），限制同时在 GPU 上飞行的帧数。
inline constexpr unsigned kFramesInFlight = 2;
}
