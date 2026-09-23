#pragma once
#include "RHI/Vulkan/VulkanContext.h"
#include "Render2D/DrawList.h"
#include <filesystem>

namespace ih::render {
// Shared SDF shape pipeline; targets own per-frame instance buffers.
class VulkanRenderer2D {
public:
    explicit VulkanRenderer2D(VulkanContext& context) : m_context(context) {}
    ~VulkanRenderer2D();
    VulkanRenderer2D(const VulkanRenderer2D&) = delete;
    VulkanRenderer2D& operator=(const VulkanRenderer2D&) = delete;
    void Initialize(const std::filesystem::path& shader, VkFormat format = VK_FORMAT_R8G8B8A8_UNORM);
    void Record(VkCommandBuffer command, VkDescriptorSet set, const render2d::DrawList& list,
        uint32_t width, uint32_t height, float scale) const;
    VkDescriptorSetLayout SetLayout() const { return m_setLayout; }
    VkPipelineLayout PipelineLayout() const { return m_pipelineLayout; }
    VkPipeline Pipeline() const { return m_pipeline; }
private:
    VulkanContext& m_context;
    VkDescriptorSetLayout m_setLayout{};
    VkPipelineLayout m_pipelineLayout{};
    VkPipeline m_pipeline{};
    bool m_linearOutput{};
};
}

