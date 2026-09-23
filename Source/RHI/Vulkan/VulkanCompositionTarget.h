#pragma once
#include "RHI/Vulkan/VulkanRenderer2D.h"
#include "RHI/Vulkan/VulkanShapeBuffer.h"
#include <memory>
#include "Platform/Windows/Win32Compositor.h"
#include <array>
#include <chrono>

namespace ih::render {
class VulkanCompositionTarget {
public:
    VulkanCompositionTarget(VulkanContext& context, VulkanRenderer2D& renderer, platform::Win32Compositor& compositor)
        : m_context(context), m_renderer(renderer), m_surface(compositor) {}
    ~VulkanCompositionTarget();
    VulkanCompositionTarget(const VulkanCompositionTarget&) = delete;
    VulkanCompositionTarget& operator=(const VulkanCompositionTarget&) = delete;
    void Initialize(HWND window, uint32_t width, uint32_t height, float dpiScale);
    bool Update(); // true = pending work; false = idle
    void RequestRedraw() { m_dirty = true; }
    void SetDrawList(render2d::DrawList list) { m_list = std::move(list); m_dirty = true; }
    uint64_t PresentedFrames() const { return m_presented; }
private:
    struct Frame {
        VkImage image{};
        VkDeviceMemory memory{};
        VkImageView view{};
        std::unique_ptr<VulkanShapeBuffer> shapes;
        VkCommandPool pool{};
        VkCommandBuffer command{};
        VkFence finished{};
    };
    void Submit(Frame& frame);
    VulkanContext& m_context;
    VulkanRenderer2D& m_renderer;
    platform::Win32CompositionSurface m_surface;
    std::array<Frame, platform::kCompositionBufferCount> m_frames{};
    render2d::DrawList m_list;
    uint32_t m_width{}, m_height{};
    float m_dpiScale = 1;
    unsigned m_frameIndex{};
    bool m_dirty = true;
    bool m_pending{};
    uint64_t m_presented{};
    std::chrono::steady_clock::time_point m_deadline;
};
}

