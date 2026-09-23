#pragma once
#include <Volk/volk.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <atomic>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace ih::experiment {
template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
inline void CheckHr(HRESULT result, const char* operation) {
    if (result != S_OK) throw std::runtime_error(std::string(operation) + ": HRESULT=" + std::to_string(static_cast<unsigned long>(result)));
}

// Isolated spike: no dependency on or changes to the product VulkanContext.
class InteropDevice {
public:
    ~InteropDevice();
    void Initialize(const std::filesystem::path& shaderPath);
    void Shutdown();
    VkInstance instance{};
    VkDebugUtilsMessengerEXT messenger{};
    VkPhysicalDevice gpu{};
    VkDevice device{};
    VkQueue queue{};
    uint32_t family{};
    VkDescriptorSetLayout setLayout{};
    VkPipelineLayout pipelineLayout{};
    VkPipeline pipeline{};
    ComPtr<IDXGIFactory2> factory;
    ComPtr<ID3D11Device> d3d;
    ComPtr<ID3D11DeviceContext> immediate;
    ComPtr<IDCompositionDevice> compositor;
    std::atomic<unsigned> errors{}, warnings{};
    std::string gpuName;
private:
    static VKAPI_ATTR VkBool32 VKAPI_CALL Debug(VkDebugUtilsMessageSeverityFlagBitsEXT,
        VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT*, void*);
};
}
