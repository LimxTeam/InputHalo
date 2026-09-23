#pragma once
#include "RHI/Vulkan/VulkanContext.h"
#include "Text/FontAtlas.h"
namespace ih::render {
// Per-frame atlas copy. Prepare may only run after the frame's fence has completed.
class VulkanFontAtlas {
public:
    explicit VulkanFontAtlas(VulkanContext& context) : m_context(context) {}
    ~VulkanFontAtlas();
    VulkanFontAtlas(const VulkanFontAtlas&) = delete;
    VulkanFontAtlas& operator=(const VulkanFontAtlas&) = delete;
    bool Prepare(std::shared_ptr<const text::Atlas> atlas);
    void RecordUpload(VkCommandBuffer command);
    VkImageView View() const { return m_view; }
    VkSampler Sampler() const { return m_sampler; }
private:
    void ClearImage();
    VkDeviceMemory Allocate(VkMemoryRequirements requirements, VkMemoryPropertyFlags flags);
    VulkanContext& m_context;
    std::shared_ptr<const text::Atlas> m_snapshot;
    VkImage m_image{};
    VkImageView m_view{};
    VkDeviceMemory m_memory{};
    VkBuffer m_staging{};
    VkDeviceMemory m_stagingMemory{};
    VkSampler m_sampler{};
    uint32_t m_size{}, m_layers{};
    bool m_pending{};
};
}
