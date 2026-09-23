/*******************************************************************************
 * 文件: VulkanShapeBuffer.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   逐帧形状缓冲实现：把 DrawList 命令数组 memcpy 进 host-visible storage buffer，
 *   并管理描述符池/描述符集，按需把字体图集视图与采样器写入描述符。
 *   不涉及复杂算法；上传为一次 memcpy + 若干描述符写。
 *   持久映射 coherent 内存，省去每帧 map/unmap，降低 CPU 开销。
 *
 * 设计哲学:
 *   缓冲容量固定为 MaxCommands×sizeof(Command)，创建后长期映射；
 *   上传只覆盖实际命令数，未用部分为脏数据但不会被 draw 访问（按 count 绘制）。
 *   图集仅在 Prepare 返回 true（图集变化）时才更新描述符，减少描述符写次数。
 *
 * 技术特性:
 *   - 持久映射: 创建即 vkMapMemory，CPU 直接写，coherent 保证立即可见。
 *   - 范围校验: 上传前校验命令数与批次区间合法性，越界即抛异常。
 *   - 描述符懒更新: 图集变化才写 binding 1/2，binding 0(缓冲)在初始化时写好。
 *
 * 依赖关系:
 *   内部: Limx::ih::render（VulkanContext/VulkanFontAtlas）、Limx::render2d
 *   外部: Vulkan 1.4、<cstring>/<stdexcept>
 *
 * 注意事项:
 *   调用方必须在 GPU 用完该缓冲前不要上传或销毁（外部同步）。
 *   coherent host-visible 内存要求：无则抛异常。
 *   析构时先 unmap 再销毁缓冲/内存。
 *
 ******************************************************************************/
#include "RHI/Vulkan/VulkanShapeBuffer.h"
#include <cstring>
#include <stdexcept>
namespace ih::render {
// Initialize: 创建 storage buffer、映射、建描述符池并分配/写入描述符集。
void VulkanShapeBuffer::Initialize(VkDescriptorSetLayout layout) {
    const auto device = m_context.Device();
// 容量 = 最大命令数 × 每条命令 64 字节，一次性按上限分配。
    constexpr VkDeviceSize capacity = render2d::MaxCommands * sizeof(render2d::Command);
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer.size = capacity; buffer.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    m_context.Check(vkCreateBuffer(device, &buffer, nullptr, &m_buffer), "2D instance buffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, m_buffer, &requirements);
// 挑选 host-visible|host-coherent 的内存类型，实现 CPU 直接写、无需手动 flush。
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(m_context.PhysicalDevice(), &properties);
    uint32_t type = properties.memoryTypeCount;
    constexpr auto flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
        if ((requirements.memoryTypeBits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags) { type = i; break; }
    if (type == properties.memoryTypeCount) throw std::runtime_error("No coherent upload memory for Renderer2D");
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = type;
    m_context.Check(vkAllocateMemory(device, &allocation, nullptr, &m_memory), "2D upload memory");
    m_context.Check(vkBindBufferMemory(device, m_buffer, m_memory, 0), "Bind 2D upload memory");
    m_context.Check(vkMapMemory(device, m_memory, 0, capacity, 0, &m_mapped), "Map 2D upload memory");
// 描述符池容纳 1 组 storage buffer + sampled image + sampler。
    const VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1}, {VK_DESCRIPTOR_TYPE_SAMPLER, 1}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = 1; pool.poolSizeCount = 3; pool.pPoolSizes = sizes;
    m_context.Check(vkCreateDescriptorPool(device, &pool, nullptr, &m_pool), "2D descriptor pool");
    VkDescriptorSetAllocateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    set.descriptorPool = m_pool; set.descriptorSetCount = 1; set.pSetLayouts = &layout;
    m_context.Check(vkAllocateDescriptorSets(device, &set, &m_set), "2D descriptor set");
// 写入 binding 0：整段 storage buffer 供着色器按实例索引读取。
    VkDescriptorBufferInfo info{m_buffer, 0, capacity};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = m_set; write.descriptorCount = 1; write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &info;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}
// Upload: 校验并把命令数组拷进映射内存，必要时刷新图集描述符。
void VulkanShapeBuffer::Upload(const render2d::DrawList& list) {
    if (list.commands.size() > render2d::MaxCommands) throw std::length_error("2D upload exceeds capacity");
    for (const auto& batch : list.batches)
        if (batch.first > list.commands.size() || batch.count > list.commands.size() - batch.first)
            throw std::invalid_argument("Invalid 2D batch range");
// 仅拷实际命令数； coherent 内存保证对 GPU 立即可见。
    if (!list.commands.empty()) std::memcpy(m_mapped, list.commands.data(), list.commands.size() * sizeof(render2d::Command));
// 图集有更新时，把新的 ImageView 与 Sampler 写入 binding 1/2。
    if (m_atlas.Prepare(list.atlas)) {
        const VkDescriptorImageInfo image{VK_NULL_HANDLE, m_atlas.View(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        const VkDescriptorImageInfo sampler{m_atlas.Sampler(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
        VkWriteDescriptorSet writes[2]{};
        for (auto& write : writes) { write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; write.dstSet = m_set; write.descriptorCount = 1; }
        writes[0].dstBinding = 1; writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; writes[0].pImageInfo = &image;
        writes[1].dstBinding = 2; writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER; writes[1].pImageInfo = &sampler;
        vkUpdateDescriptorSets(m_context.Device(), 2, writes, 0, nullptr);
    }
}
// 析构：销毁描述符池、解除映射、释放缓冲与内存。
VulkanShapeBuffer::~VulkanShapeBuffer() {
    const auto device = m_context.Device();
    if (m_pool) vkDestroyDescriptorPool(device, m_pool, nullptr);
    if (m_mapped) vkUnmapMemory(device, m_memory);
    if (m_buffer) vkDestroyBuffer(device, m_buffer, nullptr);
    if (m_memory) vkFreeMemory(device, m_memory, nullptr);
}
}
