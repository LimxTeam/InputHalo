/*******************************************************************************
 * 文件: VulkanCompositionTarget.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   合成目标声明：把 Render2D 命令流绘制到可被 Win32 合成器（D3D11 共享纹理）接收的离屏图像上，
 *   实现输入可视化 Overlay 的最终呈现；基于外部内存 + keyed mutex 跨 API 共享。
 *   不涉及复杂算法；按帧轮转缓冲，轮询 fence 判断上一帧是否可呈现。
 *   以多缓冲 + 异步提交换取 CPU 不阻塞，呈现时机由合成器侧 TryPresent 控制。
 *
 * 设计哲学:
 *   与普通交换链不同，Overlay 不直接 present，而是把帧渲染进 D3D11 共享纹理，
 *   再由 Win32Compositor 合成到目标窗口；Vulkan 侧通过 keyed mutex 与 D3D 侧互斥。
 *   采用脏标记驱动：SetDrawList/RequestRedraw 置脏，Update 非阻塞地提交并轮询 fence。
 *
 * 技术特性:
 *   - 外部内存互操作: VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT 导入共享纹理。
 *   - Keyed mutex: 提交时持锁(key 0)、释放时还锁(key 1)，与 D3D 侧同步。
 *   - 多缓冲轮转: kCompositionBufferCount 个 Frame 循环使用，避免上传冲突。
 *   - 懒提交: 仅在脏时提交，Update 返回是否有待处理工作。
 *
 * 依赖关系:
 *   内部: Limx::ih::render（VulkanContext/VulkanRenderer2D/VulkanShapeBuffer）、
 *          Limx::platform::Win32Compositor/Win32CompositionSurface、Limx::render2d::DrawList
 *   外部: Vulkan 1.4、VK_KHR_external_memory_win32、VK_KHR_win32_keyed_mutex、Win32 HWND
 *
 * 注意事项:
 *   仅在设备支持外部内存导入与 keyed mutex 时可用（见 VulkanContext::CompositionSupported）。
 *   生命周期：Frame 的 image/memory 为导入内存，不可随意释放，析构需 vkDeviceWaitIdle。
 *   稳态渲染路径不调用 vkDeviceWaitIdle（仅重建/销毁时）。
 *
 ******************************************************************************/
#pragma once
#include "RHI/Vulkan/VulkanRenderer2D.h"
#include "RHI/Vulkan/VulkanShapeBuffer.h"
#include <memory>
#include "Platform/Windows/Win32Compositor.h"
#include <array>
#include <chrono>

namespace ih::render {
// VulkanCompositionTarget: Overlay 合成目标，负责把 2D 命令绘制到 D3D11 共享纹理。
class VulkanCompositionTarget {
public:
    VulkanCompositionTarget(VulkanContext& context, VulkanRenderer2D& renderer, platform::Win32Compositor& compositor)
        : m_context(context), m_renderer(renderer), m_surface(compositor) {}
    ~VulkanCompositionTarget();
    VulkanCompositionTarget(const VulkanCompositionTarget&) = delete;
    VulkanCompositionTarget& operator=(const VulkanCompositionTarget&) = delete;
// Initialize: 按帧创建可导入的外部图像、视图、命令池/缓冲与 fence。
    void Initialize(HWND window, uint32_t width, uint32_t height, float dpiScale);
// Update: 驱动一帧（提交或轮询 fence 后呈现）；返回 true=有待处理工作，false=空闲。
    bool Update(); // true = pending work; false = idle
    void RequestRedraw() { m_dirty = true; }
    void SetDrawList(render2d::DrawList list) { m_list = std::move(list); m_dirty = true; }
    uint64_t PresentedFrames() const { return m_presented; }
private:
// Frame: 单个合成缓冲周期的资源——导入图像/内存/视图、形状缓冲、命令与完成 fence。
    struct Frame {
        VkImage image{};
        VkDeviceMemory memory{};
        VkImageView view{};
        std::unique_ptr<VulkanShapeBuffer> shapes;
        VkCommandPool pool{};
        VkCommandBuffer command{};
        VkFence finished{};
    };
// Submit: 上传命令、录制渲染并提交队列（含跨队列族与 keyed mutex 同步）。
    void Submit(Frame& frame);
    VulkanContext& m_context;
    VulkanRenderer2D& m_renderer;
    platform::Win32CompositionSurface m_surface;
// m_frames: 与合成器缓冲数一致的帧数组，循环轮转。
    std::array<Frame, platform::kCompositionBufferCount> m_frames{};
    render2d::DrawList m_list;
    uint32_t m_width{}, m_height{};
    float m_dpiScale = 1;
    unsigned m_frameIndex{};
// m_dirty: 内容是否需要重绘；m_pending: 是否已提交等待 fence。
    bool m_dirty = true;
    bool m_pending{};
    uint64_t m_presented{};
// m_deadline: 提交后等待 fence 的超时上限（默认 5s），超时抛异常。
    std::chrono::steady_clock::time_point m_deadline;
};
}

