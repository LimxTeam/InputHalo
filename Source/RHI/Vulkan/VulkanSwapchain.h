/*******************************************************************************
 * 文件: VulkanSwapchain.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   窗口交换链声明：封装 VkSwapchainKHR、图像视图、呈现信号量/fence 与逐帧渲染资源。
 *   不涉及复杂算法；声明接口与成员。
 *   双缓冲在飞帧 + 交换链图像池。
 *
 * 设计哲学:
 *   Swapchain 既是呈现载体也是 2D 内容的生产者：内部惰性持有 Renderer2D 与每帧 ShapeBuffer，
 *   对外只暴露 Draw（驱动一帧）与 SetContent（喂入着色器与 DrawList），隐藏全部同步细节。
 *
 * 技术特性:
 *   - 懒重建: 脏或尺寸变化时才 Recreate。
 *   - 能力降级: 支持 present fence 用 per-image 同步，否则退化。
 *   - RAII: Shutdown 统一回收，可重复调用安全。
 *
 * 依赖关系:
 *   内部: Limx::ih::render（VulkanContext/VulkanFrame/Renderer2D/ShapeBuffer）、
 *          Limx::render2d::DrawList
 *   外部: Vulkan 1.4(KHR_swapchain)、<memory>/<array>/<vector>/<filesystem>
 *
 * 注意事项:
 *   Draw 返回 false 可能是零尺寸或 acquire 瞬态超时，应在下一 tick 重试。
 *   对象不可拷贝，引用 VulkanContext。
 *
 ******************************************************************************/
#pragma once
#include "RHI/Vulkan/VulkanContext.h"
#include "RHI/Vulkan/VulkanFrame.h"
#include "RHI/Vulkan/VulkanShapeBuffer.h"
#include <memory>
#include <array>
#include <vector>

namespace ih::render {
// VulkanSwapchain: 窗口交换链与逐帧 2D 渲染的封装。
class VulkanSwapchain {
public:
    explicit VulkanSwapchain(VulkanContext& context) : m_context(context) {}
    ~VulkanSwapchain();
    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;
// Initialize: 创建在飞帧的命令与同步对象（不含交换链本身，交换链在 Recreate 中建）。
    std::expected<void, std::string> Initialize();
    // false means zero-sized surface or a transient acquire timeout; retry on a later tick.
// Draw: 驱动一帧；返回 false 表示零尺寸表面或瞬态 acquire 超时，稍后重试。
    std::expected<bool, std::string> Draw(uint32_t width, uint32_t height);
// SetContent: 喂入着色器路径与待绘制 DrawList（脏置位，下次 Draw 生效）。
    void SetContent(const std::filesystem::path& shader, render2d::DrawList list, float scale) {
        m_shader = shader; m_content = std::move(list); m_scale = scale;
    }
// Shutdown: 等待并释放全部交换链与帧资源（noexcept）。
    void Shutdown() noexcept;
    VkExtent2D Extent() const { return m_extent; }
    uint64_t PresentedFrames() const { return m_presentedFrames; }
private:
// 私有：Recreate 重建交换链；Drain 等待在飞工作；DestroyImages 释放图像相关资源。
    void Recreate(uint32_t width, uint32_t height);
    void Drain();
    void DestroyImages() noexcept;
    VulkanContext& m_context;
// 成员：上下文、交换链/extent/格式、图像/视图/呈现信号量与 fence 池、双缓冲帧与渲染资源。
    VkSwapchainKHR m_swapchain{};
    VkExtent2D m_extent{};
    uint32_t m_requestedWidth{}, m_requestedHeight{};
    VkFormat m_format{};
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_views;
    std::vector<VkSemaphore> m_presentReady;
    std::vector<VkFence> m_presentFinished;
    std::vector<bool> m_presentPending;
    std::array<VulkanFrame, kFramesInFlight> m_frames{};
    unsigned m_frameIndex{};
    bool m_dirty = true;
    std::filesystem::path m_shader;
    render2d::DrawList m_content;
    float m_scale = 1;
    VkFormat m_rendererFormat{};
    std::unique_ptr<VulkanRenderer2D> m_renderer;
    std::array<std::unique_ptr<VulkanShapeBuffer>, kFramesInFlight> m_shapes;
    uint64_t m_presentedFrames{};
};
}
