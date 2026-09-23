#include "InteropDevice.h"
#include "RHI/Vulkan/VulkanError.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

namespace ih::experiment {
using render::CheckVk;
VKAPI_ATTR VkBool32 VKAPI_CALL InteropDevice::Debug(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
    auto& self = *static_cast<InteropDevice*>(user);
    try {
        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) { ++self.errors; spdlog::get("Vulkan")->error("{}", data->pMessage); }
        else { ++self.warnings; spdlog::get("Vulkan")->warn("{}", data->pMessage); }
    } catch (...) {}
    return VK_FALSE;
}

void InteropDevice::Initialize(const std::filesystem::path& shaderPath) {
    CheckVk(volkInitialize(), "Volk");
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "InputHalo Compositor Interop Experiment";
    app.apiVersion = VK_API_VERSION_1_4;
    const char* layer = "VK_LAYER_KHRONOS_validation";
    const char* debugExtension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
    debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug.pfnUserCallback = Debug;
    debug.pUserData = this;
    VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    create.pApplicationInfo = &app;
    if constexpr (IH_VULKAN_VALIDATION) {
        create.enabledLayerCount = 1;
        create.ppEnabledLayerNames = &layer;
        create.enabledExtensionCount = 1;
        create.ppEnabledExtensionNames = &debugExtension;
        create.pNext = &debug;
    }
    CheckVk(vkCreateInstance(&create, nullptr, &instance), "Interop instance");
    volkLoadInstance(instance);
    if constexpr (IH_VULKAN_VALIDATION) CheckVk(vkCreateDebugUtilsMessengerEXT(instance, &debug, nullptr, &messenger), "Interop debug messenger");
    CheckHr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "DXGI factory");
    uint32_t count{};
    CheckVk(vkEnumeratePhysicalDevices(instance, &count, nullptr), "GPUs");
    std::vector<VkPhysicalDevice> devices(count);
    CheckVk(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "GPUs");
    ComPtr<IDXGIAdapter1> selectedAdapter;
    for (auto candidate : devices) {
        VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties.pNext = &id;
        vkGetPhysicalDeviceProperties2(candidate, &properties);
        if (!id.deviceLUIDValid || properties.properties.apiVersion < VK_API_VERSION_1_4) continue;
        uint32_t extensionCount{};
        CheckVk(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, nullptr), "Interop extensions");
        std::vector<VkExtensionProperties> extensions(extensionCount);
        CheckVk(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, extensions.data()), "Interop extensions");
        const auto has = [&](const char* name) { return std::ranges::any_of(extensions, [&](const auto& e) { return std::strcmp(e.extensionName, name) == 0; }); };
        if (!has(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME) || !has(VK_KHR_WIN32_KEYED_MUTEX_EXTENSION_NAME)) continue;
        VkPhysicalDeviceExternalImageFormatInfo external{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};
        external.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
        VkPhysicalDeviceImageFormatInfo2 format{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};
        format.pNext = &external;
        format.format = VK_FORMAT_R8G8B8A8_UNORM;
        format.type = VK_IMAGE_TYPE_2D;
        format.tiling = VK_IMAGE_TILING_OPTIMAL;
        format.usage = VK_IMAGE_USAGE_STORAGE_BIT;
        VkExternalImageFormatProperties externalProperties{VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
        VkImageFormatProperties2 supported{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};
        supported.pNext = &externalProperties;
        const auto formatResult = vkGetPhysicalDeviceImageFormatProperties2(candidate, &format, &supported);
        if (formatResult == VK_ERROR_FORMAT_NOT_SUPPORTED) continue;
        CheckVk(formatResult, "External image format");
        if (!(externalProperties.externalMemoryProperties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT)) continue;
        VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &features13;
        vkGetPhysicalDeviceFeatures2(candidate, &features);
        if (!features13.synchronization2) continue;
        uint32_t familyCount{};
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
        auto compute = std::ranges::find_if(families, [](const auto& f) { return (f.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0; });
        if (compute == families.end()) continue;
        for (UINT index = 0;; ++index) {
            ComPtr<IDXGIAdapter1> adapter;
            const auto result = factory->EnumAdapters1(index, &adapter);
            if (result == DXGI_ERROR_NOT_FOUND) break;
            CheckHr(result, "DXGI adapter");
            DXGI_ADAPTER_DESC1 description{};
            CheckHr(adapter->GetDesc1(&description), "DXGI adapter description");
            if (std::memcmp(&description.AdapterLuid, id.deviceLUID, VK_LUID_SIZE) == 0) {
                gpu = candidate;
                family = static_cast<uint32_t>(compute - families.begin());
                selectedAdapter = adapter;
                gpuName = properties.properties.deviceName;
                break;
            }
        }
        if (gpu) break;
    }
    if (!gpu) throw std::runtime_error("No matching Vulkan/DXGI adapter supporting D3D11 shared storage texture import and keyed mutex");
    const float priority = 1;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = family;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    const char* extensions[]{VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME, VK_KHR_WIN32_KEYED_MUTEX_EXTENSION_NAME};
    VkPhysicalDeviceVulkan13Features enabled{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    enabled.synchronization2 = VK_TRUE;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.pNext = &enabled;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = 2;
    deviceInfo.ppEnabledExtensionNames = extensions;
    CheckVk(vkCreateDevice(gpu, &deviceInfo, nullptr, &device), "Interop device");
    volkLoadDevice(device);
    vkGetDeviceQueue(device, family, 0, &queue);
    CheckHr(D3D11CreateDevice(selectedAdapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr, 0, D3D11_SDK_VERSION, &d3d, nullptr, &immediate), "D3D11 copy device");
    ComPtr<IDXGIDevice> dxgiDevice;
    CheckHr(d3d.As(&dxgiDevice), "DXGI device");
    CheckHr(DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&compositor)), "DirectComposition device");
    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setInfo.bindingCount = 1;
    setInfo.pBindings = &binding;
    CheckVk(vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &setLayout), "Storage image layout");
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, 16};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &setLayout;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;
    CheckVk(vkCreatePipelineLayout(device, &layout, nullptr, &pipelineLayout), "Compute layout");
    std::ifstream input(shaderPath, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("Cannot load InteropCircle.spv");
    const auto bytes = input.tellg();
    if (bytes <= 0 || bytes % 4 != 0) throw std::runtime_error("Invalid SPIR-V length");
    std::vector<uint32_t> code(static_cast<size_t>(bytes) / 4);
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(code.data()), bytes)) throw std::runtime_error("Cannot read SPIR-V");
    VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    moduleInfo.codeSize = static_cast<size_t>(bytes);
    moduleInfo.pCode = code.data();
    VkShaderModule module{};
    CheckVk(vkCreateShaderModule(device, &moduleInfo, nullptr, &module), "Circle shader");
    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = module;
    pipelineInfo.stage.pName = "main";
    const auto pipelineResult = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    vkDestroyShaderModule(device, module, nullptr);
    CheckVk(pipelineResult, "Circle compute pipeline");
    spdlog::get("Vulkan")->info("Interop GPU: {}; DXGI LUID matched; shared RGBA8 storage texture import supported", gpuName);
}

InteropDevice::~InteropDevice() { Shutdown(); }
void InteropDevice::Shutdown() {
    if (immediate) { immediate->ClearState(); immediate->Flush(); }
    compositor.Reset();
    if (device) {
        vkDeviceWaitIdle(device);
        if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
        if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        if (setLayout) vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
        vkDestroyDevice(device, nullptr);
        device = {};
    }
    if (messenger) { vkDestroyDebugUtilsMessengerEXT(instance, messenger, nullptr); messenger = {}; }
    if (instance) { vkDestroyInstance(instance, nullptr); instance = {}; }
}
}
