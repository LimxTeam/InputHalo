#include "InteropTarget.h"
#include "RHI/Vulkan/VulkanError.h"
#include <bit>
#include <spdlog/spdlog.h>

namespace ih::experiment {
using render::CheckVk;
void InteropTarget::Initialize(HWND window, uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;
    D3D11_TEXTURE2D_DESC texture{};
    texture.Width = width;
    texture.Height = height;
    texture.MipLevels = texture.ArraySize = 1;
    texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture.SampleDesc.Count = 1;
    texture.Usage = D3D11_USAGE_DEFAULT;
    texture.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    texture.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    CheckHr(m_device.d3d->CreateTexture2D(&texture, nullptr, &m_texture), "Shared texture");
    CheckHr(m_texture.As(&m_mutex), "Shared keyed mutex");
    ComPtr<IDXGIResource1> resource;
    CheckHr(m_texture.As(&resource), "Shared resource");
    CheckHr(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &m_sharedHandle), "Shared NT handle");
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
    image.usage = VK_IMAGE_USAGE_STORAGE_BIT;
    image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    CheckVk(vkCreateImage(m_device.device, &image, nullptr, &m_image), "Imported storage image");
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(m_device.device, m_image, &requirements);
    VkMemoryWin32HandlePropertiesKHR handleProperties{VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR};
    CheckVk(vkGetMemoryWin32HandlePropertiesKHR(m_device.device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT,
        m_sharedHandle, &handleProperties), "Imported handle properties");
    const uint32_t memoryTypes = requirements.memoryTypeBits & handleProperties.memoryTypeBits;
    if (!memoryTypes) throw std::runtime_error("No common D3D11/Vulkan memory type");
    VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    dedicated.image = m_image;
    VkImportMemoryWin32HandleInfoKHR imported{VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR};
    imported.pNext = &dedicated;
    imported.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
    imported.handle = m_sharedHandle;
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.pNext = &imported;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = static_cast<uint32_t>(std::countr_zero(memoryTypes));
    CheckVk(vkAllocateMemory(m_device.device, &allocation, nullptr, &m_memory), "Import D3D11 texture memory");
    CheckVk(vkBindImageMemory(m_device.device, m_image, m_memory, 0), "Bind imported image");
    CloseHandle(m_sharedHandle);
    m_sharedHandle = nullptr;
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = m_image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = VK_FORMAT_R8G8B8A8_UNORM;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    CheckVk(vkCreateImageView(m_device.device, &view, nullptr, &m_view), "Imported view");
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1};
    VkDescriptorPoolCreateInfo descriptors{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    descriptors.maxSets = 1;
    descriptors.poolSizeCount = 1;
    descriptors.pPoolSizes = &poolSize;
    CheckVk(vkCreateDescriptorPool(m_device.device, &descriptors, nullptr, &m_descriptors), "Descriptor pool");
    VkDescriptorSetAllocateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    set.descriptorPool = m_descriptors;
    set.descriptorSetCount = 1;
    set.pSetLayouts = &m_device.setLayout;
    CheckVk(vkAllocateDescriptorSets(m_device.device, &set, &m_set), "Storage descriptor");
    VkDescriptorImageInfo imageInfo{VK_NULL_HANDLE, m_view, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = m_set;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(m_device.device, 1, &write, 0, nullptr);
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.queueFamilyIndex = m_device.family;
    CheckVk(vkCreateCommandPool(m_device.device, &pool, nullptr, &m_pool), "Interop commands");
    VkCommandBufferAllocateInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    command.commandPool = m_pool;
    command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command.commandBufferCount = 1;
    CheckVk(vkAllocateCommandBuffers(m_device.device, &command, &m_command), "Interop command buffer");
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    CheckVk(vkCreateFence(m_device.device, &fence, nullptr, &m_finished), "Interop fence");

    DXGI_SWAP_CHAIN_DESC1 swapchain{};
    swapchain.Width = width;
    swapchain.Height = height;
    swapchain.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapchain.SampleDesc.Count = 1;
    swapchain.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapchain.BufferCount = 2;
    swapchain.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swapchain.Scaling = DXGI_SCALING_STRETCH;
    swapchain.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    CheckHr(m_device.factory->CreateSwapChainForComposition(m_device.d3d.Get(), &swapchain, nullptr, &m_swapchain), "Composition swapchain");
    CheckHr(m_device.compositor->CreateTargetForHwnd(window, TRUE, &m_target), "Composition HWND target");
    CheckHr(m_device.compositor->CreateVisual(&m_visual), "Composition visual");
    CheckHr(m_visual->SetContent(m_swapchain.Get()), "Composition content");
    CheckHr(m_target->SetRoot(m_visual.Get()), "Composition root");
    CheckHr(m_device.compositor->Commit(), "Composition commit");
}

void InteropTarget::Draw(float centerX, float centerY, float radius, float opacity, bool verify) {
    CheckVk(vkResetCommandPool(m_device.device, m_pool, 0), "Reset interop command pool");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CheckVk(vkBeginCommandBuffer(m_command, &begin), "Begin interop frame");
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // Entire shared image is overwritten.
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
    barrier.dstQueueFamilyIndex = m_device.family;
    barrier.image = m_image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(m_command, &dependency);
    const float circle[]{centerX, centerY, radius, opacity};
    vkCmdBindPipeline(m_command, VK_PIPELINE_BIND_POINT_COMPUTE, m_device.pipeline);
    vkCmdBindDescriptorSets(m_command, VK_PIPELINE_BIND_POINT_COMPUTE, m_device.pipelineLayout, 0, 1, &m_set, 0, nullptr);
    vkCmdPushConstants(m_command, m_device.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(circle), circle);
    vkCmdDispatch(m_command, (m_width + 7) / 8, (m_height + 7) / 8, 1);
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    barrier.dstAccessMask = VK_ACCESS_2_NONE;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = m_device.family;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
    vkCmdPipelineBarrier2(m_command, &dependency);
    CheckVk(vkEndCommandBuffer(m_command), "End interop frame");
    const uint64_t acquireKey = 0, releaseKey = 1;
    const uint32_t timeoutMs = 5000;
    VkWin32KeyedMutexAcquireReleaseInfoKHR mutex{VK_STRUCTURE_TYPE_WIN32_KEYED_MUTEX_ACQUIRE_RELEASE_INFO_KHR};
    mutex.acquireCount = mutex.releaseCount = 1;
    mutex.pAcquireSyncs = mutex.pReleaseSyncs = &m_memory;
    mutex.pAcquireKeys = &acquireKey;
    mutex.pReleaseKeys = &releaseKey;
    mutex.pAcquireTimeouts = &timeoutMs;
    VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    command.commandBuffer = m_command;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.pNext = &mutex;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command;
    CheckVk(vkResetFences(m_device.device, 1, &m_finished), "Reset interop fence");
    CheckVk(vkQueueSubmit2(m_device.queue, 1, &submit, m_finished), "Submit shared image");
    CheckVk(vkWaitForFences(m_device.device, 1, &m_finished, VK_TRUE, 5'000'000'000), "Wait shared image");
    CheckHr(m_mutex->AcquireSync(1, 5000), "D3D11 acquire key 1");
    try {
        ComPtr<ID3D11Texture2D> backBuffer;
        CheckHr(m_swapchain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)), "Composition buffer");
        m_device.immediate->CopyResource(backBuffer.Get(), m_texture.Get());
        if (verify) {
            D3D11_TEXTURE2D_DESC description{};
            m_texture->GetDesc(&description);
            description.Usage = D3D11_USAGE_STAGING;
            description.BindFlags = description.MiscFlags = 0;
            description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> staging;
            CheckHr(m_device.d3d->CreateTexture2D(&description, nullptr, &staging), "Test-only staging texture");
            m_device.immediate->CopyResource(staging.Get(), m_texture.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            CheckHr(m_device.immediate->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Verify shared pixels");
            const auto* base = static_cast<const unsigned char*>(mapped.pData);
            const auto* center = base + static_cast<unsigned>(centerY) * mapped.RowPitch + static_cast<unsigned>(centerX) * 4;
            for (unsigned channel = 0; channel < 4; ++channel) { cornerPixel[channel] = base[channel]; centerPixel[channel] = center[channel]; }
            m_device.immediate->Unmap(staging.Get(), 0);
            pixelsVerified = cornerPixel == std::array<unsigned, 4>{0, 0, 0, 0} && centerPixel[3] >= 210 &&
                centerPixel[3] <= 220 && centerPixel[0] >= 95 && centerPixel[0] <= 105 && centerPixel[1] >= 190 &&
                centerPixel[1] <= 200 && centerPixel[2] >= 165 && centerPixel[2] <= 175;
            if (!pixelsVerified) throw std::runtime_error("Shared pixel readback differs from premultiplied circle/transparent background");
            spdlog::get("Overlay")->info("Shared pixels verified: corner RGBA=0,0,0,0; center RGBA={},{},{},{}", centerPixel[0], centerPixel[1], centerPixel[2], centerPixel[3]);
        }
        m_device.immediate->Flush();
    } catch (...) { m_mutex->ReleaseSync(0); throw; }
    CheckHr(m_mutex->ReleaseSync(0), "D3D11 release key 0");
    CheckHr(m_swapchain->Present(1, 0), "Composition present");
}

InteropTarget::~InteropTarget() {
    if (m_device.device) vkDeviceWaitIdle(m_device.device);
    if (m_target) m_target->SetRoot(nullptr);
    if (m_device.compositor) m_device.compositor->Commit();
    m_visual.Reset();
    m_target.Reset();
    m_swapchain.Reset();
    const auto device = m_device.device;
    if (m_finished) vkDestroyFence(device, m_finished, nullptr);
    if (m_pool) vkDestroyCommandPool(device, m_pool, nullptr);
    if (m_descriptors) vkDestroyDescriptorPool(device, m_descriptors, nullptr);
    if (m_view) vkDestroyImageView(device, m_view, nullptr);
    if (m_image) vkDestroyImage(device, m_image, nullptr);
    if (m_memory) vkFreeMemory(device, m_memory, nullptr);
    if (m_sharedHandle) CloseHandle(m_sharedHandle);
}
}
