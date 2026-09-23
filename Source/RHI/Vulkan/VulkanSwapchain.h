#pragma once
#include "RHI/Vulkan/VulkanContext.h"
#include "RHI/Vulkan/VulkanFrame.h"
#include "RHI/Vulkan/VulkanShapeBuffer.h"
#include <memory>
#include <array>
#include <vector>

namespace ih::render {
class VulkanSwapchain {
public:
    explicit VulkanSwapchain(VulkanContext& context) : m_context(context) {}
    ~VulkanSwapchain();
    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;
    std::expected<void, std::string> Initialize();
    // false means zero-sized surface or a transient acquire timeout; retry on a later tick.
    std::expected<bool, std::string> Draw(uint32_t width, uint32_t height);
    void SetContent(const std::filesystem::path& shader, render2d::DrawList list, float scale) {
        m_shader = shader; m_content = std::move(list); m_scale = scale;
    }
    void Shutdown() noexcept;
    VkExtent2D Extent() const { return m_extent; }
    uint64_t PresentedFrames() const { return m_presentedFrames; }
private:
    void Recreate(uint32_t width, uint32_t height);
    void Drain();
    void DestroyImages() noexcept;
    VulkanContext& m_context;
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
