#pragma once
#include <Volk/volk.h>
#include <atomic>
#include <expected>
#include <string>

namespace ih::render {
class VulkanContext {
public:
    VulkanContext() = default;
    ~VulkanContext();
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
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
    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT* data, void* user);
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
