#include "RHI/Vulkan/VulkanShapeBuffer.h"
#include <cstring>
#include <stdexcept>
namespace ih::render {
void VulkanShapeBuffer::Initialize(VkDescriptorSetLayout layout) {
    const auto device = m_context.Device();
    constexpr VkDeviceSize capacity = render2d::MaxCommands * sizeof(render2d::Command);
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer.size = capacity; buffer.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    m_context.Check(vkCreateBuffer(device, &buffer, nullptr, &m_buffer), "2D instance buffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, m_buffer, &requirements);
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
    const VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1}, {VK_DESCRIPTOR_TYPE_SAMPLER, 1}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = 1; pool.poolSizeCount = 3; pool.pPoolSizes = sizes;
    m_context.Check(vkCreateDescriptorPool(device, &pool, nullptr, &m_pool), "2D descriptor pool");
    VkDescriptorSetAllocateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    set.descriptorPool = m_pool; set.descriptorSetCount = 1; set.pSetLayouts = &layout;
    m_context.Check(vkAllocateDescriptorSets(device, &set, &m_set), "2D descriptor set");
    VkDescriptorBufferInfo info{m_buffer, 0, capacity};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = m_set; write.descriptorCount = 1; write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &info;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}
void VulkanShapeBuffer::Upload(const render2d::DrawList& list) {
    if (list.commands.size() > render2d::MaxCommands) throw std::length_error("2D upload exceeds capacity");
    for (const auto& batch : list.batches)
        if (batch.first > list.commands.size() || batch.count > list.commands.size() - batch.first)
            throw std::invalid_argument("Invalid 2D batch range");
    if (!list.commands.empty()) std::memcpy(m_mapped, list.commands.data(), list.commands.size() * sizeof(render2d::Command));
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
VulkanShapeBuffer::~VulkanShapeBuffer() {
    const auto device = m_context.Device();
    if (m_pool) vkDestroyDescriptorPool(device, m_pool, nullptr);
    if (m_mapped) vkUnmapMemory(device, m_memory);
    if (m_buffer) vkDestroyBuffer(device, m_buffer, nullptr);
    if (m_memory) vkFreeMemory(device, m_memory, nullptr);
}
}
