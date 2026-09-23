#include "RHI/Vulkan/VulkanSwapchain.h"
#include "RHI/Vulkan/VulkanError.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <limits>
#include <string>

namespace ih::render {
namespace { constexpr uint64_t kWaitNs = 5'000'000'000; }

std::expected<void, std::string> VulkanSwapchain::Initialize() {
    try {
        const auto device = m_context.Device();
        for (unsigned index = 0; index < kFramesInFlight; ++index) {
            auto& frame = m_frames[index];
            VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pool.queueFamilyIndex = m_context.QueueFamily();
            CheckVk(vkCreateCommandPool(device, &pool, nullptr, &frame.pool), "Command pool");
            VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            allocation.commandPool = frame.pool;
            allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocation.commandBufferCount = 1;
            CheckVk(vkAllocateCommandBuffers(device, &allocation, &frame.command), "Command buffer");
            VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            CheckVk(vkCreateSemaphore(device, &semaphore, nullptr, &frame.acquired), "Acquire semaphore");
            VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            CheckVk(vkCreateFence(device, &fence, nullptr, &frame.finished), "Frame fence");
            const auto prefix = "InputHalo.Frame" + std::to_string(index);
            m_context.Name(VK_OBJECT_TYPE_COMMAND_POOL, reinterpret_cast<uint64_t>(frame.pool), (prefix + ".Pool").c_str());
            m_context.Name(VK_OBJECT_TYPE_COMMAND_BUFFER, reinterpret_cast<uint64_t>(frame.command), (prefix + ".Command").c_str());
            m_context.Name(VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<uint64_t>(frame.acquired), (prefix + ".Acquired").c_str());
            m_context.Name(VK_OBJECT_TYPE_FENCE, reinterpret_cast<uint64_t>(frame.finished), (prefix + ".Finished").c_str());
        }
        return {};
    } catch (const std::exception& error) { return std::unexpected(error.what()); }
}

void VulkanSwapchain::Drain() {
    CheckVk(vkDeviceWaitIdle(m_context.Device()), "Drain device");
    for (size_t index = 0; index < m_presentFinished.size(); ++index) {
        if (m_presentPending[index]) {
            CheckVk(vkWaitForFences(m_context.Device(), 1, &m_presentFinished[index], VK_TRUE, kWaitNs), "Drain presentation");
            m_presentPending[index] = false;
        }
    }
}

void VulkanSwapchain::DestroyImages() noexcept {
    const auto device = m_context.Device();
    for (auto view : m_views) if (view) vkDestroyImageView(device, view, nullptr);
    for (auto semaphore : m_presentReady) if (semaphore) vkDestroySemaphore(device, semaphore, nullptr);
    for (auto fence : m_presentFinished) if (fence) vkDestroyFence(device, fence, nullptr);
    m_views.clear();
    m_images.clear();
    m_presentReady.clear();
    m_presentFinished.clear();
    m_presentPending.clear();
    if (m_swapchain) vkDestroySwapchainKHR(device, m_swapchain, nullptr);
    m_swapchain = {};
}

void VulkanSwapchain::Recreate(uint32_t width, uint32_t height) {
    const auto device = m_context.Device();
    const auto gpu = m_context.PhysicalDevice();
    const auto surface = m_context.Surface();
    VkSurfaceCapabilitiesKHR capabilities{};
    CheckVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &capabilities), "Surface capabilities");
    m_extent = capabilities.currentExtent;
    if (m_extent.width == std::numeric_limits<uint32_t>::max()) {
        m_extent.width = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        m_extent.height = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }
    if (!m_extent.width || !m_extent.height) return;
    if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
        throw std::runtime_error("Surface does not support color attachments");
    uint32_t count{};
    CheckVk(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &count, nullptr), "Surface formats");
    if (!count) throw std::runtime_error("Surface has no formats");
    std::vector<VkSurfaceFormatKHR> formats(count);
    CheckVk(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &count, formats.data()), "Surface formats");
    auto format = formats.front();
    if (format.format == VK_FORMAT_UNDEFINED) format.format = VK_FORMAT_B8G8R8A8_SRGB;
    for (const auto candidate : formats) {
        if (candidate.format == VK_FORMAT_B8G8R8A8_SRGB && candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            format = candidate;
            break;
        }
    }
    CheckVk(vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &count, nullptr), "Present modes");
    std::vector<VkPresentModeKHR> modes(count);
    CheckVk(vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &count, modes.data()), "Present modes");
    const auto presentMode = std::ranges::find(modes, VK_PRESENT_MODE_MAILBOX_KHR) != modes.end() ?
        VK_PRESENT_MODE_MAILBOX_KHR : VK_PRESENT_MODE_FIFO_KHR;
    VkCompositeAlphaFlagBitsKHR alpha{};
    // This is the normal, opaque settings window. Overlay policy comes in Milestone 2.
    for (auto candidate : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) {
        if (capabilities.supportedCompositeAlpha & candidate) { alpha = candidate; break; }
    }
    if (!alpha) throw std::runtime_error("No supported composite alpha mode");
    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount) imageCount = std::min(imageCount, capabilities.maxImageCount);
    Drain();
    VkSwapchainCreateInfoKHR create{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    create.surface = surface;
    create.minImageCount = imageCount;
    create.imageFormat = format.format;
    create.imageColorSpace = format.colorSpace;
    create.imageExtent = m_extent;
    create.imageArrayLayers = 1;
    create.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    create.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create.preTransform = capabilities.currentTransform;
    create.compositeAlpha = alpha;
    create.presentMode = presentMode;
    create.clipped = VK_TRUE;
    create.oldSwapchain = m_swapchain;
    VkSwapchainKHR replacement{};
    CheckVk(vkCreateSwapchainKHR(device, &create, nullptr, &replacement), "Create swapchain");
    DestroyImages();
    m_swapchain = replacement;
    m_format = format.format;
    m_context.Name(VK_OBJECT_TYPE_SWAPCHAIN_KHR, reinterpret_cast<uint64_t>(m_swapchain), "InputHalo.SettingsSwapchain");
    CheckVk(vkGetSwapchainImagesKHR(device, m_swapchain, &count, nullptr), "Swapchain images");
    m_images.resize(count);
    CheckVk(vkGetSwapchainImagesKHR(device, m_swapchain, &count, m_images.data()), "Swapchain images");
    m_views.resize(count);
    m_presentReady.resize(count);
    if (m_context.PresentFences()) { m_presentFinished.resize(count); m_presentPending.resize(count, false); }
    for (uint32_t index = 0; index < count; ++index) {
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = m_images[index];
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = m_format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        CheckVk(vkCreateImageView(device, &view, nullptr, &m_views[index]), "Swapchain image view");
        VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        CheckVk(vkCreateSemaphore(device, &semaphore, nullptr, &m_presentReady[index]), "Present semaphore");
        const auto prefix = "InputHalo.SettingsImage" + std::to_string(index);
        m_context.Name(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(m_images[index]), prefix.c_str());
        m_context.Name(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(m_views[index]), (prefix + ".View").c_str());
        m_context.Name(VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<uint64_t>(m_presentReady[index]), (prefix + ".PresentReady").c_str());
        if (m_context.PresentFences()) {
            VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            CheckVk(vkCreateFence(device, &fence, nullptr, &m_presentFinished[index]), "Present fence");
            m_context.Name(VK_OBJECT_TYPE_FENCE, reinterpret_cast<uint64_t>(m_presentFinished[index]), (prefix + ".PresentFinished").c_str());
        }
    }
    m_requestedWidth = width;
    m_requestedHeight = height;
    m_dirty = false;
    spdlog::get("Vulkan")->info("Settings swapchain: {}x{}, {} images, format {}, {}, alpha mask={} selected={}",
        m_extent.width, m_extent.height, count, static_cast<int>(m_format),
        presentMode == VK_PRESENT_MODE_MAILBOX_KHR ? "MAILBOX" : "FIFO", capabilities.supportedCompositeAlpha, static_cast<int>(alpha));
}

std::expected<bool, std::string> VulkanSwapchain::Draw(uint32_t width, uint32_t height) {
    if (!width || !height) return false;
    try {
        if (m_dirty || width != m_requestedWidth || height != m_requestedHeight) Recreate(width, height);
        if (!m_extent.width || !m_extent.height) return false;
        const auto device = m_context.Device();
        auto& frame = m_frames[m_frameIndex];
        CheckVk(vkWaitForFences(device, 1, &frame.finished, VK_TRUE, kWaitNs), "Wait frame");
        if (!m_shader.empty() && (!m_renderer || m_rendererFormat != m_format)) {
            Drain();
            for (auto& shapes : m_shapes) shapes.reset();
            m_renderer = std::make_unique<VulkanRenderer2D>(m_context);
            m_renderer->Initialize(m_shader, m_format);
            for (auto& shapes : m_shapes) {
                shapes = std::make_unique<VulkanShapeBuffer>(m_context);
                shapes->Initialize(m_renderer->SetLayout());
            }
            m_rendererFormat = m_format;
        }
        if (m_renderer) m_shapes[m_frameIndex]->Upload(m_content);
        uint32_t imageIndex{};
        const auto acquired = vkAcquireNextImageKHR(device, m_swapchain, 100'000'000, frame.acquired, VK_NULL_HANDLE, &imageIndex);
        if (acquired == VK_ERROR_OUT_OF_DATE_KHR) { m_dirty = true; return false; }
        if (acquired == VK_TIMEOUT || acquired == VK_NOT_READY) return false;
        if (acquired != VK_SUBOPTIMAL_KHR) CheckVk(acquired, "Acquire image");
        if (m_context.PresentFences() && m_presentPending[imageIndex]) {
            CheckVk(vkWaitForFences(device, 1, &m_presentFinished[imageIndex], VK_TRUE, kWaitNs), "Wait present fence");
            m_presentPending[imageIndex] = false;
        }
        CheckVk(vkResetCommandPool(device, frame.pool, 0), "Reset command pool");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        CheckVk(vkBeginCommandBuffer(frame.command, &begin), "Begin commands");
        if (m_renderer) m_shapes[m_frameIndex]->RecordUploads(frame.command);
        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // Clear discards the previous contents.
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_images[imageIndex];
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(frame.command, &dependency);
        VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        color.imageView = m_views[imageIndex];
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.clearValue.color = {{0.035f, 0.045f, 0.065f, 1.0f}};
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        rendering.renderArea.extent = m_extent;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;
        vkCmdBeginRendering(frame.command, &rendering);
        if (m_renderer) m_renderer->Record(frame.command, m_shapes[m_frameIndex]->Set(), m_content, m_extent.width, m_extent.height, m_scale);
        vkCmdEndRendering(frame.command);
        barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
        barrier.dstAccessMask = VK_ACCESS_2_NONE;
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier2(frame.command, &dependency);
        CheckVk(vkEndCommandBuffer(frame.command), "End commands");
        VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        wait.semaphore = frame.acquired;
        wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        signal.semaphore = m_presentReady[imageIndex];
        signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        command.commandBuffer = frame.command;
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.waitSemaphoreInfoCount = 1;
        submit.pWaitSemaphoreInfos = &wait;
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &command;
        submit.signalSemaphoreInfoCount = 1;
        submit.pSignalSemaphoreInfos = &signal;
        // Only reset after a successful acquire/record; an out-of-date acquire never strands a fence.
        CheckVk(vkResetFences(device, 1, &frame.finished), "Reset frame fence");
        CheckVk(vkQueueSubmit2(m_context.Queue(), 1, &submit, frame.finished), "Submit frame");
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &m_presentReady[imageIndex];
        present.swapchainCount = 1;
        present.pSwapchains = &m_swapchain;
        present.pImageIndices = &imageIndex;
        VkSwapchainPresentFenceInfoEXT presentFence{VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT};
        if (m_context.PresentFences()) {
            CheckVk(vkResetFences(device, 1, &m_presentFinished[imageIndex]), "Reset present fence");
            presentFence.swapchainCount = 1;
            presentFence.pFences = &m_presentFinished[imageIndex];
            present.pNext = &presentFence;
        }
        const auto result = vkQueuePresentKHR(m_context.Queue(), &present);
        if (result != VK_SUBOPTIMAL_KHR && result != VK_ERROR_OUT_OF_DATE_KHR) CheckVk(result, "Present frame");
        if (m_context.PresentFences()) m_presentPending[imageIndex] = true;
        m_dirty = result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR || acquired == VK_SUBOPTIMAL_KHR;
        m_frameIndex = (m_frameIndex + 1) % kFramesInFlight;
        if (result != VK_ERROR_OUT_OF_DATE_KHR) ++m_presentedFrames;
        return !m_dirty;
    } catch (const std::exception& error) { return std::unexpected(error.what()); }
}

VulkanSwapchain::~VulkanSwapchain() { Shutdown(); }
void VulkanSwapchain::Shutdown() noexcept {
    if (!m_context.Device()) return;
    try { Drain(); }
    catch (const std::exception& error) {
        try { spdlog::get("Vulkan")->error("{}", error.what()); } catch (...) {}
    }
    DestroyImages();
    for (auto& frame : m_frames) {
        if (frame.finished) vkDestroyFence(m_context.Device(), frame.finished, nullptr);
        if (frame.acquired) vkDestroySemaphore(m_context.Device(), frame.acquired, nullptr);
        if (frame.pool) vkDestroyCommandPool(m_context.Device(), frame.pool, nullptr);
        frame = {};
    }
    for (auto& shapes : m_shapes) shapes.reset();
    m_renderer.reset();
    m_content = {}; m_frameIndex = 0; m_dirty = true; m_rendererFormat = {};
}
}
