#pragma once
#include "RHI/Vulkan/VulkanRenderer2D.h"
#include "RHI/Vulkan/VulkanFontAtlas.h"
namespace ih::render {
// The owner must wait for GPU use to finish before uploading or destroying this buffer.
class VulkanShapeBuffer {
public:
    explicit VulkanShapeBuffer(VulkanContext& context) : m_context(context), m_atlas(context) {}
    ~VulkanShapeBuffer();
    VulkanShapeBuffer(const VulkanShapeBuffer&) = delete;
    VulkanShapeBuffer& operator=(const VulkanShapeBuffer&) = delete;
    void Initialize(VkDescriptorSetLayout layout);
    void Upload(const render2d::DrawList& list);
    void RecordUploads(VkCommandBuffer command) { m_atlas.RecordUpload(command); }
    VkDescriptorSet Set() const { return m_set; }
private:
    VulkanContext& m_context;
    VkBuffer m_buffer{};
    VkDeviceMemory m_memory{};
    VkDescriptorPool m_pool{};
    VkDescriptorSet m_set{};
    void* m_mapped{};
    VulkanFontAtlas m_atlas;
};
}

