/*******************************************************************************
 * 文件: VulkanFontAtlas.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   字体图集实现：把 CPU 端 text::Atlas 上传为 GPU 的 2D 数组图像（R8 单通道 SDF/灰度），
 *   管理 staging 缓冲、图像视图与采样器，并在命令缓冲中录制 buffer->image 拷贝与布局迁移。
 *   不涉及复杂算法；每页一次 memcpy + 一次拷贝命令。
 *   用 host-visible staging 中转到 device-local image，换取采样性能，代价是一次额外拷贝。
 *
 * 设计哲学:
 *   图集按帧复用：Prepare 仅在图集变化（revision/指针）时重建资源；
 *   由于 Prepare 一定在该帧 fence 完成后调用，staging 缓冲可安全复用并在空闲后回收。
 *   多层图集用 array layers 存放多个 page，采样器 ClampToEdge + 线性过滤。
 *
 * 技术特性:
 *   - 延迟重建: 图集未变时 Prepare 返回 false，避免重复上传。
 *   - 两阶段同步: 录制时先切到 TRANSFER_DST 拷贝，再切到 SHADER_READ_ONLY 供片元采样。
 *   - 内存选型: 图像走 device-local，staging 走 host-visible|coherent。
 *   - 资源自清理: ClearImage 统一释放图像/视图/staging，sampler 复用不重建。
 *
 * 依赖关系:
 *   内部: Limx::ih::render（VulkanContext）、Limx::text::Atlas
 *   外部: Vulkan 1.4、<cstring>/<stdexcept>
 *
 * 注意事项:
 *   Prepare 调用前提：该帧 fence 已完成，否则会与在飞的上传冲突。
 *   图集尺寸 1..4096、页数 1..8，每页字节数须等于 size*size，否则抛异常。
 *   空图集（atlas==nullptr）创建一张全 0 的占位图，保证着色器有合法视图可采样。
 *
 ******************************************************************************/
#include "RHI/Vulkan/VulkanFontAtlas.h"
#include <cstring>
#include <stdexcept>
namespace ih::render {
// Allocate: 在满足需求类型位与期望属性的堆上分配一块设备内存。
VkDeviceMemory VulkanFontAtlas::Allocate(VkMemoryRequirements requirements, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(m_context.PhysicalDevice(), &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        if (!(requirements.memoryTypeBits & (1u << i)) || (properties.memoryTypes[i].propertyFlags & flags) != flags) continue;
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = i;
        VkDeviceMemory result{};
        m_context.Check(vkAllocateMemory(m_context.Device(), &allocation, nullptr, &result), "Atlas memory");
        return result;
    }
    throw std::runtime_error("No compatible font atlas memory");
}
// ClearImage: 释放图像/视图/staging 等帧级资源并清空快照与 pending 标志。
void VulkanFontAtlas::ClearImage() {
    const auto device = m_context.Device();
    if (m_view) vkDestroyImageView(device, m_view, nullptr);
    if (m_image) vkDestroyImage(device, m_image, nullptr);
    if (m_memory) vkFreeMemory(device, m_memory, nullptr);
    if (m_staging) vkDestroyBuffer(device, m_staging, nullptr);
    if (m_stagingMemory) vkFreeMemory(device, m_stagingMemory, nullptr);
    m_view = {}; m_image = {}; m_memory = {}; m_staging = {}; m_stagingMemory = {};
    m_snapshot.reset(); m_pending = false;
}
// 析构：先清图像资源，再释放复用的采样器。
VulkanFontAtlas::~VulkanFontAtlas() {
    ClearImage();
    if (m_sampler) vkDestroySampler(m_context.Device(), m_sampler, nullptr);
}
// Prepare: 图集未变化则回收空闲 staging 并返回 false；变化则重建 GPU 图集。
bool VulkanFontAtlas::Prepare(std::shared_ptr<const text::Atlas> atlas) {
    if (m_image && atlas == m_snapshot) {
        // Prepare is called only after this frame's fence completes. The upload buffer
        // is no longer needed once the previous submission consumed it.
// 上一帧已消费 staging 且不再 pending，即可销毁释放显存写回带宽。
        if(!m_pending&&m_staging){
            vkDestroyBuffer(m_context.Device(),m_staging,nullptr);
            vkFreeMemory(m_context.Device(),m_stagingMemory,nullptr);
            m_staging={};m_stagingMemory={};
        }
        return false;
    }
// 校验图集尺寸与页数合法性，避免异常布局进入 GPU。
    if (atlas && (atlas->size < 1 || atlas->size > 4096 || atlas->pages.empty() || atlas->pages.size() > 8))
        throw std::invalid_argument("Invalid atlas dimensions");
    if (atlas) for (const auto& page : atlas->pages)
        if (page.size() != static_cast<size_t>(atlas->size) * atlas->size) throw std::invalid_argument("Invalid atlas pixels");
    ClearImage();
    const auto device = m_context.Device();
// 确定图集边长与层数（页数）；空图集退化为 1x1 单层占位。
    m_size = atlas ? atlas->size : 1;
    m_layers = atlas ? static_cast<uint32_t>(atlas->pages.size()) : 1;
// 采样器只建一次：线性过滤 + ClampToEdge，跨图集重建保持复用。
    if (!m_sampler) {
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        m_context.Check(vkCreateSampler(device, &sampler, nullptr, &m_sampler), "Font sampler");
    }
// 创建 R8 2D 数组图像，用途为拷贝目标 + 着色器采样。
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType = VK_IMAGE_TYPE_2D; image.format = VK_FORMAT_R8_UNORM;
    image.extent = {m_size, m_size, 1}; image.mipLevels = 1; image.arrayLayers = m_layers;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    m_context.Check(vkCreateImage(device, &image, nullptr, &m_image), "Font atlas image");
// 分配 device-local 内存并绑定图像，再建覆盖所有层的 ImageView。
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, m_image, &requirements);
    m_memory = Allocate(requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    m_context.Check(vkBindImageMemory(device, m_image, m_memory, 0), "Bind atlas image");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = m_image; view.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY; view.format = image.format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, m_layers};
    m_context.Check(vkCreateImageView(device, &view, nullptr, &m_view), "Font atlas view");
// 创建 host-visible staging 缓冲，大小 = 每页字节数 × 层数。
    const VkDeviceSize pageBytes = static_cast<VkDeviceSize>(m_size) * m_size;
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer.size = pageBytes * m_layers; buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    m_context.Check(vkCreateBuffer(device, &buffer, nullptr, &m_staging), "Atlas staging buffer");
    vkGetBufferMemoryRequirements(device, m_staging, &requirements);
    m_stagingMemory = Allocate(requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    m_context.Check(vkBindBufferMemory(device, m_staging, m_stagingMemory, 0), "Bind atlas staging memory");
// 映射 staging，逐页 memcpy 像素；空图集填 0，随后取消映射。
    void* mapped{};
    m_context.Check(vkMapMemory(device, m_stagingMemory, 0, buffer.size, 0, &mapped), "Map atlas staging");
    if (atlas) for (uint32_t layer = 0; layer < m_layers; ++layer)
        std::memcpy(static_cast<uint8_t*>(mapped) + layer * pageBytes, atlas->pages[layer].data(), static_cast<size_t>(pageBytes));
    else *static_cast<uint8_t*>(mapped) = 0;
    vkUnmapMemory(device, m_stagingMemory);
// 记录新快照并置 pending，等待 RecordUpload 真正提交拷贝。
    m_snapshot = std::move(atlas); m_pending = true;
    return true;
}
// RecordUpload: 把 staging 拷进图像并迁移到着色器只读布局（仅在 pending 时）。
void VulkanFontAtlas::RecordUpload(VkCommandBuffer command) {
    if (!m_pending) return;
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.image = m_image; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, m_layers};
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT; barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1; dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(command, &dependency);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, m_layers}; copy.imageExtent = {m_size, m_size, 1};
    vkCmdCopyBufferToImage(command, m_staging, m_image, barrier.newLayout, 1, &copy);
    barrier.oldLayout = barrier.newLayout; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT; barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT; barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    vkCmdPipelineBarrier2(command, &dependency);
    m_pending = false;
}
}
