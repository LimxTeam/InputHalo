/*******************************************************************************
 * 文件: VulkanContext.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   Vulkan 渲染上下文声明：持有 Instance/Device/Queue/Surface 等核心对象，
 *   负责物理设备选型、特性协商、调试信使与错误/设备丢失状态追踪。
 *   不涉及复杂算法；设备选型为带权重的枚举扫描。
 *   用一个中心对象聚合所有 Vulkan 句柄，避免各模块重复初始化。
 *
 * 设计哲学:
 *   VulkanContext 是整个 RHI 层的根：其它对象都引用它来取设备/队列与上报错误。
 *   初始化以 std::expected 暴露错误字符串，便于上层把启动失败转成用户可读信息。
 *   设备丢失与验证层错误用原子计数，可在任意线程查询。
 *
 * 技术特性:
 *   - RAII 生命周期: 构造空态，Initialize 建对象，Shutdown/析构释放。
 *   - 不可拷贝: 删除拷贝构造/赋值，防止句柄被双重释放。
 *   - 调试对象命名: Name() 在验证开启时给 Vulkan 对象打标签。
 *   - 特性探测: 动态渲染、synchronization2、交换链维护1、外部内存等按支持情况降级。
 *
 * 依赖关系:
 *   内部: Limx::ih::render
 *   外部: Vulkan 1.4、Volk、Win32 HINSTANCE/HWND、spdlog（仅 cpp）
 *
 * 注意事项:
 *   Initialize 失败时返回 unexpected，部分资源可能已创建需自行检查返回值。
 *   Shutdown 为 noexcept，内部吞掉日志异常。
 *   仅在 IH_VULKAN_VALIDATION 构建下启用验证层与调试信使。
 *
 ******************************************************************************/
#pragma once
#include <Volk/volk.h>
#include <atomic>
#include <expected>
#include <string>

namespace ih::render {
// VulkanContext: Vulkan 核心上下文，聚合 instance/device/queue/surface 与状态标志。
class VulkanContext {
public:
    VulkanContext() = default;
    ~VulkanContext();
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
// Initialize: 加载 Volk、建 instance/surface、枚举并打分选择 GPU、建 device。
// requestComposition 为 true 时额外探测 D3D11 外部内存互操作能力。
    std::expected<void, std::string> Initialize(HINSTANCE instance, HWND window, bool requestComposition = false);
    bool CompositionSupported() const { return m_compositionSupported; }
    bool DeviceLost() const { return m_deviceLost; }
    void Check(VkResult result, const char* operation);
    void Shutdown() noexcept;
    void Name(VkObjectType type, uint64_t handle, const char* name) const;
    VkInstance Instance() const { return m_instance; }
    VkPhysicalDevice PhysicalDevice() const { return m_physicalDevice; }
    VkDevice Device() const { return m_device; }
    VkSurfaceKHR Surface() const { return m_surface; }
    VkQueue Queue() const { return m_queue; }
    uint32_t QueueFamily() const { return m_queueFamily; }
    bool PresentFences() const { return m_presentFences; }
    unsigned ValidationErrors() const { return m_errors.load(); }
    unsigned ValidationWarnings() const { return m_warnings.load(); }
private:
// DebugCallback: 验证层消息回调（静态），按严重级别累加错误/警告计数并打日志。
    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT* data, void* user);
// 成员：Vulkan 句柄与状态标志；errors/warnings 为原子计数，跨线程可读。
    VkInstance m_instance{};
    VkDebugUtilsMessengerEXT m_messenger{};
    VkSurfaceKHR m_surface{};
    VkPhysicalDevice m_physicalDevice{};
    VkDevice m_device{};
    VkQueue m_queue{};
    uint32_t m_queueFamily{};
    bool m_presentFences{};
    bool m_compositionSupported{};
    bool m_deviceLost{};
    std::atomic<unsigned> m_errors{};
    std::atomic<unsigned> m_warnings{};
};
}
