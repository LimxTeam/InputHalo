#include "RHI/Vulkan/VulkanFontAtlas.h"
#include <cstring>
#include <stdexcept>
namespace ih::render {
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
VulkanFontAtlas::~VulkanFontAtlas() {
    ClearImage();
    if (m_sampler) vkDestroySampler(m_context.Device(), m_sampler, nullptr);
}
bool VulkanFontAtlas::Prepare(std::shared_ptr<const text::Atlas> atlas) {
    if (m_image && atlas == m_snapshot) {
        // Prepare is called only after this frame's fence completes. The upload buffer
        // is no longer needed once the previous submission consumed it.
        if(!m_pending&&m_staging){
            vkDestroyBuffer(m_context.Device(),m_staging,nullptr);
            vkFreeMemory(m_context.Device(),m_stagingMemory,nullptr);
            m_staging={};m_stagingMemory={};
        }
        return false;
    }
    if (atlas && (atlas->size < 1 || atlas->size > 4096 || atlas->pages.empty() || atlas->pages.size() > 8))
        throw std::invalid_argument("Invalid atlas dimensions");
    if (atlas) for (const auto& page : atlas->pages)
        if (page.size() != static_cast<size_t>(atlas->size) * atlas->size) throw std::invalid_argument("Invalid atlas pixels");
    ClearImage();
    const auto device = m_context.Device();
    m_size = atlas ? atlas->size : 1;
    m_layers = atlas ? static_cast<uint32_t>(atlas->pages.size()) : 1;
    if (!m_sampler) {
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        m_context.Check(vkCreateSampler(device, &sampler, nullptr, &m_sampler), "Font sampler");
    }
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType = VK_IMAGE_TYPE_2D; image.format = VK_FORMAT_R8_UNORM;
    image.extent = {m_size, m_size, 1}; image.mipLevels = 1; image.arrayLayers = m_layers;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    m_context.Check(vkCreateImage(device, &image, nullptr, &m_image), "Font atlas image");
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, m_image, &requirements);
    m_memory = Allocate(requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    m_context.Check(vkBindImageMemory(device, m_image, m_memory, 0), "Bind atlas image");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = m_image; view.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY; view.format = image.format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, m_layers};
    m_context.Check(vkCreateImageView(device, &view, nullptr, &m_view), "Font atlas view");
    const VkDeviceSize pageBytes = static_cast<VkDeviceSize>(m_size) * m_size;
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer.size = pageBytes * m_layers; buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    m_context.Check(vkCreateBuffer(device, &buffer, nullptr, &m_staging), "Atlas staging buffer");
    vkGetBufferMemoryRequirements(device, m_staging, &requirements);
    m_stagingMemory = Allocate(requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    m_context.Check(vkBindBufferMemory(device, m_staging, m_stagingMemory, 0), "Bind atlas staging memory");
    void* mapped{};
    m_context.Check(vkMapMemory(device, m_stagingMemory, 0, buffer.size, 0, &mapped), "Map atlas staging");
    if (atlas) for (uint32_t layer = 0; layer < m_layers; ++layer)
        std::memcpy(static_cast<uint8_t*>(mapped) + layer * pageBytes, atlas->pages[layer].data(), static_cast<size_t>(pageBytes));
    else *static_cast<uint8_t*>(mapped) = 0;
    vkUnmapMemory(device, m_stagingMemory);
    m_snapshot = std::move(atlas); m_pending = true;
    return true;
}
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
