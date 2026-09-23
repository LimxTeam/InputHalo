/*******************************************************************************
 * 文件: VulkanFontAtlas.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   字体图集声明：负责 text::Atlas 的 GPU 侧镜像（图像/视图/采样器/staging）。
 *   不涉及复杂算法；仅声明接口与资源句柄。
 *   按帧持有一套资源，时间换空间：图集变化才重建。
 *
 * 设计哲学:
 *   与帧缓冲绑定：每帧一个 VulkanFontAtlas，Prepare 必须在该帧 fence 完成后调用，
 *   从而安全复用 staging；视图/采样器暴露给 VulkanShapeBuffer 绑定到描述符集。
 *
 * 技术特性:
 *   - 帧级生命周期: 与某一帧的在飞命令严格配对。
 *   - 延迟上传: Prepare 决定是否重建，RecordUpload 才录制实际拷贝。
 *   - 只读暴露: View()/Sampler() 供描述符写入。
 *
 * 依赖关系:
 *   内部: Limx::ih::render（VulkanContext）、Limx::text::Atlas
 *   外部: Vulkan(Volk)、<memory>
 *
 * 注意事项:
 *   未在 fence 完成前调用 Prepare 会与在飞上传产生数据竞争。
 *   对象不可拷贝，引用 VulkanContext 不拥有其生命周期。
 *
 ******************************************************************************/
#pragma once
#include "RHI/Vulkan/VulkanContext.h"
#include "Text/FontAtlas.h"
namespace ih::render {
// Per-frame atlas copy. Prepare may only run after the frame's fence has completed.
// VulkanFontAtlas: 每帧一份的字体图集 GPU 镜像（见上方注释：Prepare 需在 fence 完成后）。
class VulkanFontAtlas {
public:
    explicit VulkanFontAtlas(VulkanContext& context) : m_context(context) {}
    ~VulkanFontAtlas();
    VulkanFontAtlas(const VulkanFontAtlas&) = delete;
    VulkanFontAtlas& operator=(const VulkanFontAtlas&) = delete;
// Prepare: 比较图集快照，必要时重建资源；返回 true 表示本次确实上传了新图集。
    bool Prepare(std::shared_ptr<const text::Atlas> atlas);
// RecordUpload: 在命令缓冲中录制 buffer->image 拷贝与布局迁移。
    void RecordUpload(VkCommandBuffer command);
    VkImageView View() const { return m_view; }
    VkSampler Sampler() const { return m_sampler; }
private:
// ClearImage: 释放图像/视图/staging（内部复用）。
    void ClearImage();
// Allocate: 按需求与属性标志分配设备内存。
    VkDeviceMemory Allocate(VkMemoryRequirements requirements, VkMemoryPropertyFlags flags);
    VulkanContext& m_context;
// 成员：上下文引用、图集快照、图像/视图/内存/staging/采样器句柄与尺寸/pending 状态。
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
