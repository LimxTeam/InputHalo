/*******************************************************************************
 * 文件: VulkanCompositionTarget.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   合成目标实现：创建 D3D11 共享纹理的 Vulkan 外部内存镜像、录制并提交渲染命令、
 *   通过 keyed mutex 在 Vulkan 与 D3D11 间同步、轮询 fence 后交给 Win32 合成器呈现。
 *   不涉及复杂算法；每帧一次录制+提交，fence 轮询为 O(1)。
 *   以导入外部内存避免 GPU 间拷贝，代价是必须严格遵守 keyed mutex 协议。
 *
 * 设计哲学:
 *   图像从 D3D11 共享句柄导入（不分配新显存），渲染前后用 2 个 pipeline barrier 完成
 *   外部队列族 -> 图形队列族、再 -> 外部队列族的所有权移交；提交时附带 keyed mutex 使。
 *   Update 采用非阻塞轮询：脏时提交并记录 deadline，之后每 tick 查 fence 状态决定是否呈现。
 *
 * 技术特性:
 *   - 导入内存: vkAllocateMemory 时挂 VkImportMemoryWin32HandleInfoKHR。
 *   - Dedicated allocation: 对外部图像使用专用分配，兼容共享纹理布局。
 *   - Synchronization2: 使用 VkImageMemoryBarrier2 / VkSubmitInfo2 做细粒度同步。
 *   - 调试命名: 为每个 Vulkan 对象设置 debug name，便于验证层定位。
 *
 * 依赖关系:
 *   内部: Limx::ih::render、Limx::platform::Win32CompositionSurface
 *   外部: Vulkan 1.4、VK_KHR_external_memory_win32、VK_KHR_win32_keyed_mutex
 *
 * 注意事项:
 *   memory 为导入句柄，vkFreeMemory 不会释放 D3D 侧纹理。
 *   析构时必须 vkDeviceWaitIdle；稳态渲染路径禁止等待设备空闲。
 *   若 5 秒内 fence 未完成，Update 抛运行时超时。
 *
 ******************************************************************************/
#include "RHI/Vulkan/VulkanCompositionTarget.h"
#include <bit>
#include <stdexcept>

namespace ih::render {
// Initialize: 逐帧创建外部可共享图像、导入其内存、建视图与命令资源。
void VulkanCompositionTarget::Initialize(HWND window, uint32_t width, uint32_t height, float dpiScale) {
    m_width = width;
    m_height = height;
    m_dpiScale = dpiScale;
    m_surface.Initialize(window, width, height);
    const auto device = m_context.Device();
    const auto prefix = "InputHalo.Overlay." + std::to_string(reinterpret_cast<uintptr_t>(window));
    for (unsigned index = 0; index < m_frames.size(); ++index) {
        auto& frame = m_frames[index];
// 声明图像将用 D3D11 共享句柄作为外部内存类型（VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT）。
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
// 创建 Vulkan 端 VkImage（不分配显存，稍后导入 D3D 内存绑定）。
        m_context.Check(vkCreateImage(device, &image, nullptr, &frame.image), "Overlay imported image");
// 查询图像内存需求，并向驱动询问该共享句柄支持的内存类型位。
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device, frame.image, &requirements);
        VkMemoryWin32HandlePropertiesKHR properties{VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR};
        m_context.Check(vkGetMemoryWin32HandlePropertiesKHR(device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT,
            m_surface.SharedHandle(index), &properties), "Overlay memory handle");
// 取 Vulkan 需求与 Win32 句柄属性的交集；无交集则说明无法跨 API 共享。
        const uint32_t types = requirements.memoryTypeBits & properties.memoryTypeBits;
        if (!types) throw std::runtime_error("No compatible shared texture memory type");
// 专用分配 + 导入 Win32 句柄：把 D3D11 纹理内存挂到该 VkImage 上。
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
// 分配并绑定导入内存，再创建 2D ImageView 作为颜色附件目标。
        m_context.Check(vkAllocateMemory(device, &memory, nullptr, &frame.memory), "Import overlay texture memory");
        m_context.Check(vkBindImageMemory(device, frame.image, frame.memory, 0), "Bind overlay memory");
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = frame.image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = VK_FORMAT_R8G8B8A8_UNORM;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        m_context.Check(vkCreateImageView(device, &view, nullptr, &frame.view), "Overlay image view");
// 每帧持有独立的 VulkanShapeBuffer（实例缓冲/图集/描述符集），避免多帧上传冲突。
        frame.shapes = std::make_unique<VulkanShapeBuffer>(m_context);
        frame.shapes->Initialize(m_renderer.SetLayout());
// 为该帧创建命令池与主命令缓冲，并创建完成 fence（用于轮询 GPU 是否画完）。
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
// 为本帧各 Vulkan 对象设置调试名，便于验证层与图形调试器识别。
        const auto name = prefix + ".Frame" + std::to_string(index);
        m_context.Name(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(frame.image), (name + ".Image").c_str());
        m_context.Name(VK_OBJECT_TYPE_DEVICE_MEMORY, reinterpret_cast<uint64_t>(frame.memory), (name + ".Memory").c_str());
        m_context.Name(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(frame.view), (name + ".View").c_str());
        m_context.Name(VK_OBJECT_TYPE_COMMAND_POOL, reinterpret_cast<uint64_t>(frame.pool), (name + ".Pool").c_str());
        m_context.Name(VK_OBJECT_TYPE_COMMAND_BUFFER, reinterpret_cast<uint64_t>(frame.command), (name + ".Command").c_str());
        m_context.Name(VK_OBJECT_TYPE_FENCE, reinterpret_cast<uint64_t>(frame.finished), (name + ".Fence").c_str());
    }
}

// Submit: 上传命令数据、重置命令池、录制渲染并提交队列。
void VulkanCompositionTarget::Submit(Frame& frame) {
    const auto device = m_context.Device();
    frame.shapes->Upload(m_list);
// 复用命令池前必须重置；ONE_TIME_SUBMIT 表示该缓冲只录制一次即提交。
    m_context.Check(vkResetCommandPool(device, frame.pool, 0), "Reset overlay pool");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    m_context.Check(vkBeginCommandBuffer(frame.command, &begin), "Begin overlay commands");
    frame.shapes->RecordUploads(frame.command);
// Barrier 1: 外部队列族 -> 本图形队列族，布局 UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL。
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
// 切换到附件写入阶段；随后开始动态渲染（无 RenderPass），加载操作为 CLEAR。
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
// 调用共享的 2D 管线录制所有批次（按 scissor 分组绘制）。
    m_renderer.Record(frame.command, frame.shapes->Set(), m_list, m_width, m_height, m_dpiScale);
    vkCmdEndRendering(frame.command);
// Barrier 2: 本队列族 -> 外部队列族，布局转为 GENERAL，供 D3D 侧读取/合成。
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
// Keyed mutex: 提交附带 acquire(key0)/release(key1)，与 D3D 侧互斥访问共享纹理。
    const uint64_t acquireKey = 0, releaseKey = 1;
    const uint32_t timeoutMs = 3000;
    VkWin32KeyedMutexAcquireReleaseInfoKHR mutex{VK_STRUCTURE_TYPE_WIN32_KEYED_MUTEX_ACQUIRE_RELEASE_INFO_KHR};
    mutex.acquireCount = mutex.releaseCount = 1;
    mutex.pAcquireSyncs = mutex.pReleaseSyncs = &frame.memory;
    mutex.pAcquireKeys = &acquireKey;
    mutex.pReleaseKeys = &releaseKey;
    mutex.pAcquireTimeouts = &timeoutMs;
// 带 mutex 信息提交命令缓冲，并用 frame.finished fence 标记本次提交完成。
    VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    command.commandBuffer = frame.command;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.pNext = &mutex;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command;
    m_context.Check(vkResetFences(device, 1, &frame.finished), "Reset overlay fence");
    m_context.Check(vkQueueSubmit2(m_context.Queue(), 1, &submit, frame.finished), "Submit overlay");
}

// Update: 非阻塞驱动——脏则提交；否则轮询 fence，就绪则 TryPresent 并轮转帧。
bool VulkanCompositionTarget::Update() {
    if (!m_pending) {
        if (!m_dirty) return false;
        Submit(m_frames[m_frameIndex]);
        m_pending = true;
        m_dirty = false;
        m_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        return true;
    }
// 超过 deadline 仍未完成视为呈现超时，直接抛异常。
    if (std::chrono::steady_clock::now() > m_deadline) throw std::runtime_error("Overlay presentation timed out");
    const auto result = vkGetFenceStatus(m_context.Device(), m_frames[m_frameIndex].finished);
    if (result == VK_NOT_READY) return true;
    m_context.Check(result, "Poll overlay fence");
// TryPresent 失败说明合成器侧尚未就绪，本帧不计入 presented，下一 tick 再试。
    if (!m_surface.TryPresent(m_frameIndex)) return true;
    ++m_presented;
    m_pending = false;
    m_frameIndex = (m_frameIndex + 1) % platform::kCompositionBufferCount;
    return m_dirty;
}

// 析构：先等待设备空闲，再逆序释放帧资源（导入内存亦可安全 free）。
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

