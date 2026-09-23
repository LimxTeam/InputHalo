#include "RHI/Vulkan/VulkanCompositionTarget.h"
#include <bit>
#include <stdexcept>

namespace ih::render {
void VulkanCompositionTarget::Initialize(HWND window, uint32_t width, uint32_t height, float dpiScale) {
    m_width = width;
    m_height = height;
    m_dpiScale = dpiScale;
    m_surface.Initialize(window, width, height);
    const auto device = m_context.Device();
    const auto prefix = "InputHalo.Overlay." + std::to_string(reinterpret_cast<uintptr_t>(window));
    for (unsigned index = 0; index < m_frames.size(); ++index) {
        auto& frame = m_frames[index];
        VkExternalMemoryImageCreateInfo external{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
        external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
        VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image.pNext = &external;
        image.imageType = VK_IMAGE_TYPE_2D;
        image.format = VK_FORMAT_R8G8B8A8_UNORM;
        image.extent = {width, height, 1};
        image.mipLevels = image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        m_context.Check(vkCreateImage(device, &image, nullptr, &frame.image), "Overlay imported image");
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device, frame.image, &requirements);
        VkMemoryWin32HandlePropertiesKHR properties{VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR};
        m_context.Check(vkGetMemoryWin32HandlePropertiesKHR(device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT,
            m_surface.SharedHandle(index), &properties), "Overlay memory handle");
        const uint32_t types = requirements.memoryTypeBits & properties.memoryTypeBits;
        if (!types) throw std::runtime_error("No compatible shared texture memory type");
        VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
        dedicated.image = frame.image;
        VkImportMemoryWin32HandleInfoKHR imported{VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR};
        imported.pNext = &dedicated;
        imported.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
        imported.handle = m_surface.SharedHandle(index);
        VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        memory.pNext = &imported;
        memory.allocationSize = requirements.size;
        memory.memoryTypeIndex = static_cast<uint32_t>(std::countr_zero(types));
        m_context.Check(vkAllocateMemory(device, &memory, nullptr, &frame.memory), "Import overlay texture memory");
        m_context.Check(vkBindImageMemory(device, frame.image, frame.memory, 0), "Bind overlay memory");
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = frame.image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = VK_FORMAT_R8G8B8A8_UNORM;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        m_context.Check(vkCreateImageView(device, &view, nullptr, &frame.view), "Overlay image view");
        frame.shapes = std::make_unique<VulkanShapeBuffer>(m_context);
        frame.shapes->Initialize(m_renderer.SetLayout());
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.queueFamilyIndex = m_context.QueueFamily();
        m_context.Check(vkCreateCommandPool(device, &pool, nullptr, &frame.pool), "Overlay command pool");
        VkCommandBufferAllocateInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        command.commandPool = frame.pool;
        command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command.commandBufferCount = 1;
        m_context.Check(vkAllocateCommandBuffers(device, &command, &frame.command), "Overlay commands");
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        m_context.Check(vkCreateFence(device, &fence, nullptr, &frame.finished), "Overlay fence");
        const auto name = prefix + ".Frame" + std::to_string(index);
        m_context.Name(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(frame.image), (name + ".Image").c_str());
        m_context.Name(VK_OBJECT_TYPE_DEVICE_MEMORY, reinterpret_cast<uint64_t>(frame.memory), (name + ".Memory").c_str());
        m_context.Name(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(frame.view), (name + ".View").c_str());
        m_context.Name(VK_OBJECT_TYPE_COMMAND_POOL, reinterpret_cast<uint64_t>(frame.pool), (name + ".Pool").c_str());
        m_context.Name(VK_OBJECT_TYPE_COMMAND_BUFFER, reinterpret_cast<uint64_t>(frame.command), (name + ".Command").c_str());
        m_context.Name(VK_OBJECT_TYPE_FENCE, reinterpret_cast<uint64_t>(frame.finished), (name + ".Fence").c_str());
    }
}

void VulkanCompositionTarget::Submit(Frame& frame) {
    const auto device = m_context.Device();
    frame.shapes->Upload(m_list);
    m_context.Check(vkResetCommandPool(device, frame.pool, 0), "Reset overlay pool");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    m_context.Check(vkBeginCommandBuffer(frame.command, &begin), "Begin overlay commands");
    frame.shapes->RecordUploads(frame.command);
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
    barrier.dstQueueFamilyIndex = m_context.QueueFamily();
    barrier.image = frame.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(frame.command, &dependency);
    VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    attachment.imageView = frame.view;
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent = {m_width, m_height};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &attachment;
    vkCmdBeginRendering(frame.command, &rendering);
    m_renderer.Record(frame.command, frame.shapes->Set(), m_list, m_width, m_height, m_dpiScale);
    vkCmdEndRendering(frame.command);
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    barrier.dstAccessMask = VK_ACCESS_2_NONE;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = m_context.QueueFamily();
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
    vkCmdPipelineBarrier2(frame.command, &dependency);
    m_context.Check(vkEndCommandBuffer(frame.command), "End overlay commands");
    const uint64_t acquireKey = 0, releaseKey = 1;
    const uint32_t timeoutMs = 3000;
    VkWin32KeyedMutexAcquireReleaseInfoKHR mutex{VK_STRUCTURE_TYPE_WIN32_KEYED_MUTEX_ACQUIRE_RELEASE_INFO_KHR};
    mutex.acquireCount = mutex.releaseCount = 1;
    mutex.pAcquireSyncs = mutex.pReleaseSyncs = &frame.memory;
    mutex.pAcquireKeys = &acquireKey;
    mutex.pReleaseKeys = &releaseKey;
    mutex.pAcquireTimeouts = &timeoutMs;
    VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    command.commandBuffer = frame.command;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.pNext = &mutex;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command;
    m_context.Check(vkResetFences(device, 1, &frame.finished), "Reset overlay fence");
    m_context.Check(vkQueueSubmit2(m_context.Queue(), 1, &submit, frame.finished), "Submit overlay");
}

bool VulkanCompositionTarget::Update() {
    if (!m_pending) {
        if (!m_dirty) return false;
        Submit(m_frames[m_frameIndex]);
        m_pending = true;
        m_dirty = false;
        m_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        return true;
    }
    if (std::chrono::steady_clock::now() > m_deadline) throw std::runtime_error("Overlay presentation timed out");
    const auto result = vkGetFenceStatus(m_context.Device(), m_frames[m_frameIndex].finished);
    if (result == VK_NOT_READY) return true;
    m_context.Check(result, "Poll overlay fence");
    if (!m_surface.TryPresent(m_frameIndex)) return true;
    ++m_presented;
    m_pending = false;
    m_frameIndex = (m_frameIndex + 1) % platform::kCompositionBufferCount;
    return m_dirty;
}

VulkanCompositionTarget::~VulkanCompositionTarget() {
    const auto device = m_context.Device();
    vkDeviceWaitIdle(device); // Only teardown/rebuild; never on the steady-state rendering path.
    for (auto& frame : m_frames) {
        frame.shapes.reset();
        if (frame.finished) vkDestroyFence(device, frame.finished, nullptr);
        if (frame.pool) vkDestroyCommandPool(device, frame.pool, nullptr);
        if (frame.view) vkDestroyImageView(device, frame.view, nullptr);
        if (frame.image) vkDestroyImage(device, frame.image, nullptr);
        if (frame.memory) vkFreeMemory(device, frame.memory, nullptr);
    }
}
}

