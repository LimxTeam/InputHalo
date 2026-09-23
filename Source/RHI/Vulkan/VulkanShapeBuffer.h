/*******************************************************************************
 * 文件: VulkanShapeBuffer.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   逐帧形状缓冲声明：实例 storage buffer + 描述符集 + 内嵌字体图集。
 *   不涉及复杂算法；仅声明接口。
 *   每帧一份，固定容量，持久映射。
 *
 * 设计哲学:
 *   把"命令数据缓冲"与"其绑定的图集资源"聚合成一个可上传单元，
 *   Renderer2D 录制时只需取该帧的 Set() 即可拿到完整描述符集。
 *
 * 技术特性:
 *   - 组合图集: 内嵌 VulkanFontAtlas，上传时联动 Prepare/RecordUpload。
 *   - 外部同步: 文档化要求调用方在 GPU 用完前不得上传/销毁。
 *
 * 依赖关系:
 *   内部: Limx::ih::render、Limx::render2d::DrawList
 *   外部: Vulkan(Volk)
 *
 * 注意事项:
 *   GPU 在用时上传会造成撕裂；务必由帧 fence 守护。
 *   对象不可拷贝。
 *
 ******************************************************************************/
#pragma once
#include "RHI/Vulkan/VulkanRenderer2D.h"
#include "RHI/Vulkan/VulkanFontAtlas.h"
namespace ih::render {
// The owner must wait for GPU use to finish before uploading or destroying this buffer.
// VulkanShapeBuffer: 逐帧实例缓冲；调用方须等待 GPU 用完再上传/销毁（见上方注释）。
class VulkanShapeBuffer {
public:
    explicit VulkanShapeBuffer(VulkanContext& context) : m_context(context), m_atlas(context) {}
    ~VulkanShapeBuffer();
    VulkanShapeBuffer(const VulkanShapeBuffer&) = delete;
    VulkanShapeBuffer& operator=(const VulkanShapeBuffer&) = delete;
// Initialize: 给定描述符集布局，创建缓冲/内存/描述符集。
    void Initialize(VkDescriptorSetLayout layout);
// Upload: 把 DrawList 命令拷入映射内存并按需刷新图集描述符。
    void Upload(const render2d::DrawList& list);
// RecordUploads: 转发图集上传录制（在命令缓冲中）。
    void RecordUploads(VkCommandBuffer command) { m_atlas.RecordUpload(command); }
    VkDescriptorSet Set() const { return m_set; }
private:
// 成员：上下文、缓冲/内存/描述符池/描述符集、持久映射指针与内嵌图集。
    VulkanContext& m_context;
    VkBuffer m_buffer{};
    VkDeviceMemory m_memory{};
    VkDescriptorPool m_pool{};
    VkDescriptorSet m_set{};
    void* m_mapped{};
    VulkanFontAtlas m_atlas;
};
}

