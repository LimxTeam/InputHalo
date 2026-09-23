/*******************************************************************************
 * 文件: VulkanRenderer2D.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   共享 SDF 形状管线声明：统一的 2D 图形管线与录制接口。
 *   不涉及复杂算法；仅声明接口。
 *   管线全应用共享，各帧目标自带实例缓冲，空间换复用。
 *
 * 设计哲学:
 *   Renderer2D 持有管线/布局等重量级且可跨目标复用的对象；
 *   帧相关的实例缓冲与图集放在 VulkanShapeBuffer 中，实现"一份管线、多份帧数据"。
 *
 * 技术特性:
 *   - 可共享管线: 一次创建，多目标录制。
 *   - 动态视口: Record 接受宽高/scale，适配不同 DPI 与目标。
 *
 * 依赖关系:
 *   内部: Limx::ih::render、Limx::render2d::DrawList
 *   外部: Vulkan(Volk)、<filesystem>
 *
 * 注意事项:
 *   Initialize 必须在任何 Record 之前调用，并传入 SPIR-V 着色器路径。
 *   对象不可拷贝，引用 VulkanContext。
 *
 ******************************************************************************/
#pragma once
#include "RHI/Vulkan/VulkanContext.h"
#include "Render2D/DrawList.h"
#include <filesystem>

namespace ih::render {
// Shared SDF shape pipeline; targets own per-frame instance buffers.
// VulkanRenderer2D: 共享的 SDF 形状管线；各渲染目标自带逐帧实例缓冲。
class VulkanRenderer2D {
public:
    explicit VulkanRenderer2D(VulkanContext& context) : m_context(context) {}
    ~VulkanRenderer2D();
    VulkanRenderer2D(const VulkanRenderer2D&) = delete;
    VulkanRenderer2D& operator=(const VulkanRenderer2D&) = delete;
// Initialize: 加载 SPIR-V 并按给定附件格式构建管线。
    void Initialize(const std::filesystem::path& shader, VkFormat format = VK_FORMAT_R8G8B8A8_UNORM);
// Record: 在命令缓冲上绑定管线并按 DrawList 批次录制绘制。
    void Record(VkCommandBuffer command, VkDescriptorSet set, const render2d::DrawList& list,
        uint32_t width, uint32_t height, float scale) const;
    VkDescriptorSetLayout SetLayout() const { return m_setLayout; }
    VkPipelineLayout PipelineLayout() const { return m_pipelineLayout; }
    VkPipeline Pipeline() const { return m_pipeline; }
private:
// 成员：上下文引用、描述符集布局/管线布局/管线句柄与线性输出标志。
    VulkanContext& m_context;
    VkDescriptorSetLayout m_setLayout{};
    VkPipelineLayout m_pipelineLayout{};
    VkPipeline m_pipeline{};
    bool m_linearOutput{};
};
}

