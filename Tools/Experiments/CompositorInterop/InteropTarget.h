#pragma once
#include "InteropDevice.h"
#include <array>

namespace ih::experiment {
class InteropTarget {
public:
    explicit InteropTarget(InteropDevice& device) : m_device(device) {}
    InteropTarget(const InteropTarget&) = delete;
    InteropTarget& operator=(const InteropTarget&) = delete;
    ~InteropTarget();
    void Initialize(HWND window, uint32_t width, uint32_t height);
    void Draw(float centerX, float centerY, float radius, float opacity, bool verify);
    bool pixelsVerified{};
    std::array<unsigned, 4> centerPixel{}, cornerPixel{};
private:
    InteropDevice& m_device;
    uint32_t m_width{}, m_height{};
    ComPtr<ID3D11Texture2D> m_texture;
    ComPtr<IDXGIKeyedMutex> m_mutex;
    ComPtr<IDXGISwapChain1> m_swapchain;
    ComPtr<IDCompositionTarget> m_target;
    ComPtr<IDCompositionVisual> m_visual;
    HANDLE m_sharedHandle{};
    VkImage m_image{};
    VkDeviceMemory m_memory{};
    VkImageView m_view{};
    VkDescriptorPool m_descriptors{};
    VkDescriptorSet m_set{};
    VkCommandPool m_pool{};
    VkCommandBuffer m_command{};
    VkFence m_finished{};
};
}
