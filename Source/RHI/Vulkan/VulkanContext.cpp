/*******************************************************************************
 * 文件: VulkanContext.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   Vulkan 上下文实现：Volk 初始化、Instance/扩展/验证层创建、Win32 Surface 创建、
 *   物理设备打分选型、队列族与特性协商、Device 创建，以及错误检查/对象命名/资源释放。
 *   不涉及复杂算法；设备选型为加权枚举 O(GPU×family)。
 *   以运行时扩展/特性探测换取最大可移植性，缺失能力时优雅降级而非硬失败。
 *
 * 设计哲学:
 *   初始化包在 try/catch 中并以 std::expected 返回，保证构造失败时不残留半初始化状态；
 *   打分优先独显、再要求图形+present，支持外部内存合成者额外加分；
 *   验证层仅在 IH_VULKAN_VALIDATION 下启用，Release 构建零开销。
 *
 * 技术特性:
 *   - Loader 协商: 先 volkInitialize 再要求实例版本 >= 1.4。
 *   - 扩展按需启用: surface maintenance / debug utils / swapchain maintenance / external memory。
 *   - Synchronization2 + DynamicRendering 为硬性要求（管线核心依赖）。
 *   - 错误路由: Check() 捕获 DEVICE_LOST 并置标志，其余经 CheckVk 抛异常。
 *
 * 依赖关系:
 *   内部: Limx::ih::render、VulkanError.h(CheckVk)
 *   外部: Vulkan 1.4、Volk、spdlog、Win32 surface
 *
 * 注意事项:
 *   找不到满足 Vulkan1.4+动态渲染+同步2+交换链的 GPU 会抛错。
 *   回调内绝不向外抛异常（catch(...) 吞掉），避免穿越 Vulkan 回调栈展开。
 *   Shutdown 顺序：device -> surface -> messenger -> instance。
 *
 ******************************************************************************/
#include "RHI/Vulkan/VulkanContext.h"
#include "RHI/Vulkan/VulkanError.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstring>
#include <vector>

namespace ih::render {
// DebugCallback: 验证层回调；错误/警告分别计数并写日志；catch 所有异常防止跨回调展开。
VKAPI_ATTR VkBool32 VKAPI_CALL VulkanContext::DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
    auto& self = *static_cast<VulkanContext*>(user);
    try {
        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
            ++self.m_errors;
            spdlog::get("Vulkan")->error("Validation: {}", data->pMessage);
        } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
            ++self.m_warnings;
            spdlog::get("Vulkan")->warn("Validation: {}", data->pMessage);
        }
    } catch (...) { /* Never unwind through a Vulkan callback. */ }
    return VK_FALSE;
}

// Initialize: 完整初始化 Vulkan 栈，失败以 std::unexpected 返回错误描述。
std::expected<void, std::string> VulkanContext::Initialize(HINSTANCE instance, HWND window, bool requestComposition) {
    try {
// 初始化 Volk 加载器并校验运行时 Vulkan 版本 >= 1.4。
        CheckVk(volkInitialize(), "volkInitialize");
        if (volkGetInstanceVersion() < VK_API_VERSION_1_4)
            throw std::runtime_error("Vulkan 1.4 loader is required");
// 默认启用 surface 与 Win32 surface 扩展；按需追加 surface maintenance 扩展。
        std::vector<const char*> extensions{VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
        uint32_t instanceExtensionCount{};
        CheckVk(vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, nullptr), "Instance extensions");
        std::vector<VkExtensionProperties> instanceExtensions(instanceExtensionCount);
        CheckVk(vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, instanceExtensions.data()), "Instance extensions");
        const auto hasExtension = [&](const char* name) {
            return std::ranges::any_of(instanceExtensions, [&](const auto& e) { return std::strcmp(e.extensionName, name) == 0; });
        };
        const bool surfaceMaintenance = hasExtension(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME) &&
            hasExtension(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
        if (surfaceMaintenance) {
            extensions.push_back(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
            extensions.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
        }
// 验证层（仅调试构建）：枚举层、确认存在 Khronos 验证层后启用 debug utils。
        const char* layer = "VK_LAYER_KHRONOS_validation";
        VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debug.pfnUserCallback = DebugCallback;
        debug.pUserData = this;
        if constexpr (IH_VULKAN_VALIDATION) {
            uint32_t count{};
            CheckVk(vkEnumerateInstanceLayerProperties(&count, nullptr), "Enumerate layers");
            std::vector<VkLayerProperties> layers(count);
            CheckVk(vkEnumerateInstanceLayerProperties(&count, layers.data()), "Enumerate layers");
            if (std::ranges::none_of(layers, [&](const auto& entry) { return std::strcmp(entry.layerName, layer) == 0; }))
                throw std::runtime_error("Debug requires VK_LAYER_KHRONOS_validation; install the Vulkan SDK validation component");
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
// 填写 ApplicationInfo 并创建 Instance；调试构建挂验证层与调试信使。
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "InputHalo";
        app.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
        app.apiVersion = VK_API_VERSION_1_4;
        VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        create.pApplicationInfo = &app;
        create.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        create.ppEnabledExtensionNames = extensions.data();
        if constexpr (IH_VULKAN_VALIDATION) {
            create.enabledLayerCount = 1;
            create.ppEnabledLayerNames = &layer;
            create.pNext = &debug;
        }
        CheckVk(vkCreateInstance(&create, nullptr, &m_instance), "vkCreateInstance");
// instance 创建后加载实例级函数指针；再创建 Win32 surface。
        volkLoadInstance(m_instance);
        if constexpr (IH_VULKAN_VALIDATION)
            CheckVk(vkCreateDebugUtilsMessengerEXT(m_instance, &debug, nullptr, &m_messenger), "Debug messenger");
        VkWin32SurfaceCreateInfoKHR surface{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
        surface.hinstance = instance;
        surface.hwnd = window;
        CheckVk(vkCreateWin32SurfaceKHR(m_instance, &surface, nullptr, &m_surface), "Win32 surface");

// 枚举物理 GPU，逐个评估是否满足本引擎的硬性/软性需求。
        uint32_t count{};
        CheckVk(vkEnumeratePhysicalDevices(m_instance, &count, nullptr), "Enumerate GPUs");
        std::vector<VkPhysicalDevice> devices(count);
        CheckVk(vkEnumeratePhysicalDevices(m_instance, &count, devices.data()), "Enumerate GPUs");
        int bestScore = -1;
// 设备打分：要求 Vulkan1.4、动态渲染、synchronization2、交换链扩展。
        for (const auto candidate : devices) {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(candidate, &properties);
            if (properties.apiVersion < VK_API_VERSION_1_4) continue;
            VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
            VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            features.pNext = &features13;
            vkGetPhysicalDeviceFeatures2(candidate, &features);
            if (!features13.dynamicRendering || !features13.synchronization2) continue;
            uint32_t extensionCount{};
            CheckVk(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, nullptr), "Device extensions");
            std::vector<VkExtensionProperties> deviceExtensions(extensionCount);
            CheckVk(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, deviceExtensions.data()), "Device extensions");
            if (std::ranges::none_of(deviceExtensions, [](const auto& e) { return std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0; })) continue;
// 若请求合成，进一步探测外部内存(Win32)+keyed mutex+D3D11 可导入格式支持。
            bool composition = requestComposition;
            for (const auto* name : {VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME, VK_KHR_WIN32_KEYED_MUTEX_EXTENSION_NAME})
                composition &= std::ranges::any_of(deviceExtensions, [&](const auto& e) { return std::strcmp(e.extensionName, name) == 0; });
            if (composition) {
                VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
                VkPhysicalDeviceProperties2 identity{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
                identity.pNext = &id;
                vkGetPhysicalDeviceProperties2(candidate, &identity);
                VkPhysicalDeviceExternalImageFormatInfo external{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};
                external.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
                VkPhysicalDeviceImageFormatInfo2 format{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};
                format.pNext = &external;
                format.format = VK_FORMAT_R8G8B8A8_UNORM;
                format.type = VK_IMAGE_TYPE_2D;
                format.tiling = VK_IMAGE_TILING_OPTIMAL;
                format.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
                VkExternalImageFormatProperties imported{VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
                VkImageFormatProperties2 supported{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};
                supported.pNext = &imported;
                const auto status = vkGetPhysicalDeviceImageFormatProperties2(candidate, &format, &supported);
                if (status != VK_ERROR_FORMAT_NOT_SUPPORTED) CheckVk(status, "Composition image format");
                composition = id.deviceLUIDValid && status == VK_SUCCESS &&
                    (imported.externalMemoryProperties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT);
            }
// 遍历队列族，挑出支持 present 且含图形能力的最高分组作为展示队列族。
            uint32_t familyCount{};
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
            std::vector<VkQueueFamilyProperties> families(familyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
            for (uint32_t index = 0; index < familyCount; ++index) {
                VkBool32 present{};
                CheckVk(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, index, m_surface, &present), "Surface support");
                const bool queueComposition = composition && (families[index].queueFlags & VK_QUEUE_COMPUTE_BIT);
                const int score = (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1) + (queueComposition ? 4 : 0);
                if (present && (families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) && score > bestScore) {
                    bestScore = score;
                    m_physicalDevice = candidate;
                    m_queueFamily = index;
                    m_compositionSupported = queueComposition;
                    m_presentFences = surfaceMaintenance && std::ranges::any_of(deviceExtensions, [](const auto& e) {
                        return std::strcmp(e.extensionName, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME) == 0;
                    });
                }
            }
        }
        if (!m_physicalDevice) throw std::runtime_error("No Vulkan 1.4 GPU with graphics/present, dynamic rendering and synchronization2");
// 若启用 present fences，再次确认 swapchainMaintenance1 特性真实可用。
        VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT maintenance{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT};
        if (m_presentFences) {
            VkPhysicalDeviceFeatures2 supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            supported.pNext = &maintenance;
            vkGetPhysicalDeviceFeatures2(m_physicalDevice, &supported);
            m_presentFences = maintenance.swapchainMaintenance1 == VK_TRUE;
        }
// 创建逻辑设备：启用动态渲染/同步2，按能力挂外部内存与交换链维护扩展。
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue.queueFamilyIndex = m_queueFamily;
        queue.queueCount = 1;
        queue.pQueuePriorities = &priority;
        VkPhysicalDeviceVulkan13Features enabled{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        enabled.dynamicRendering = VK_TRUE;
        enabled.synchronization2 = VK_TRUE;
        if (m_presentFences) enabled.pNext = &maintenance;
        std::vector<const char*> deviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        if (m_compositionSupported) {
            deviceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
            deviceExtensions.push_back(VK_KHR_WIN32_KEYED_MUTEX_EXTENSION_NAME);
        }
        if (m_presentFences) deviceExtensions.push_back(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
        VkDeviceCreateInfo device{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        device.pNext = &enabled;
        device.queueCreateInfoCount = 1;
        device.pQueueCreateInfos = &queue;
        device.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        device.ppEnabledExtensionNames = deviceExtensions.data();
// device 创建后加载设备级函数指针，取队列并给设备/队列打调试名。
        CheckVk(vkCreateDevice(m_physicalDevice, &device, nullptr, &m_device), "vkCreateDevice");
        volkLoadDevice(m_device);
        vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);
        Name(VK_OBJECT_TYPE_DEVICE, reinterpret_cast<uint64_t>(m_device), "InputHalo.Device");
        Name(VK_OBJECT_TYPE_QUEUE, reinterpret_cast<uint64_t>(m_queue), "InputHalo.GraphicsPresentQueue");
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
        spdlog::get("Vulkan")->info("GPU: {}; API {}.{}.{}; validation {}; present fences {}", properties.deviceName,
            VK_API_VERSION_MAJOR(properties.apiVersion), VK_API_VERSION_MINOR(properties.apiVersion),
            VK_API_VERSION_PATCH(properties.apiVersion), IH_VULKAN_VALIDATION ? "ON" : "OFF", m_presentFences);
        if (requestComposition) spdlog::get("Vulkan")->info("Composition interop support: {}", m_compositionSupported);
        return {};
    } catch (const std::exception& error) { return std::unexpected(error.what()); }
}

// Check: 包装 CheckVk，额外把 DEVICE_LOST 记为永久设备丢失状态。
void VulkanContext::Check(VkResult result, const char* operation) {
    if (result == VK_ERROR_DEVICE_LOST) m_deviceLost = true;
    CheckVk(result, operation);
}

// Name: 仅在调试构建设置 Vulkan 对象调试名，Release 为空操作。
void VulkanContext::Name(VkObjectType type, uint64_t handle, const char* name) const {
    if constexpr (IH_VULKAN_VALIDATION) {
        if (!vkSetDebugUtilsObjectNameEXT) return;
        VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
        info.objectType = type;
        info.objectHandle = handle;
        info.pObjectName = name;
        CheckVk(vkSetDebugUtilsObjectNameEXT(m_device, &info), "Debug object name");
    }
}

// 析构转交给 Shutdown，保证幂等释放。
VulkanContext::~VulkanContext() { Shutdown(); }
// Shutdown: 按 device->surface->messenger->instance 顺序释放，并输出验证层统计。
void VulkanContext::Shutdown() noexcept {
    if (m_device) { vkDeviceWaitIdle(m_device); vkDestroyDevice(m_device, nullptr); m_device = {}; }
    if (m_surface) { vkDestroySurfaceKHR(m_instance, m_surface, nullptr); m_surface = {}; }
    if (m_messenger) { vkDestroyDebugUtilsMessengerEXT(m_instance, m_messenger, nullptr); m_messenger = {}; }
    if (m_instance) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = {};
        try { spdlog::get("Vulkan")->info("Shutdown; validation errors={}, warnings={}", m_errors.load(), m_warnings.load()); }
        catch (...) {}
    }
}
}

