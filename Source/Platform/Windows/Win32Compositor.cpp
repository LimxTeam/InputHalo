#include "Platform/Windows/Win32Compositor.h"
#include <cstring>
#include <stdexcept>
#include <string>

namespace ih::platform {
namespace {
void CheckHr(HRESULT status, const char* operation) {
    if (status != S_OK) throw std::runtime_error(std::string(operation) + " (HRESULT " + std::to_string(static_cast<uint32_t>(status)) + ")");
}
}
void Win32Compositor::Initialize(const std::array<uint8_t, 8>& adapterLuid) {
    CheckHr(CreateDXGIFactory1(IID_PPV_ARGS(&m_factory)), "Create DXGI factory");
    ComPtr<IDXGIAdapter1> selected;
    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> adapter;
        const auto result = m_factory->EnumAdapters1(index, &adapter);
        if (result == DXGI_ERROR_NOT_FOUND) break;
        CheckHr(result, "Enumerate DXGI adapter");
        DXGI_ADAPTER_DESC1 description{};
        CheckHr(adapter->GetDesc1(&description), "DXGI adapter description");
        static_assert(sizeof(description.AdapterLuid) == 8);
        if (std::memcmp(&description.AdapterLuid, adapterLuid.data(), 8) == 0) { selected = adapter; break; }
    }
    if (!selected) throw std::runtime_error("No DXGI adapter matching the Vulkan device LUID");
    CheckHr(D3D11CreateDevice(selected.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr, 0, D3D11_SDK_VERSION, &m_device, nullptr, &m_immediate), "Create composition copy device");
    ComPtr<IDXGIDevice> dxgiDevice;
    CheckHr(m_device.As(&dxgiDevice), "Get DXGI device");
    CheckHr(DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&m_composition)), "Create DirectComposition device");
}

void Win32CompositionSurface::Initialize(HWND window, uint32_t width, uint32_t height) {
    D3D11_TEXTURE2D_DESC texture{};
    texture.Width = width;
    texture.Height = height;
    texture.MipLevels = texture.ArraySize = 1;
    texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture.SampleDesc.Count = 1;
    texture.Usage = D3D11_USAGE_DEFAULT;
    texture.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    texture.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    for (auto& frame : m_frames) {
        CheckHr(m_compositor.Device()->CreateTexture2D(&texture, nullptr, &frame.texture), "Create shared overlay texture");
        CheckHr(frame.texture.As(&frame.mutex), "Get shared texture mutex");
        ComPtr<IDXGIResource1> resource;
        CheckHr(frame.texture.As(&resource), "Get shared texture resource");
        CheckHr(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
            nullptr, &frame.handle), "Create shared texture handle");
    }
    DXGI_SWAP_CHAIN_DESC1 swapchain{};
    swapchain.Width = width;
    swapchain.Height = height;
    swapchain.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapchain.SampleDesc.Count = 1;
    swapchain.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapchain.BufferCount = kCompositionBufferCount;
    swapchain.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swapchain.Scaling = DXGI_SCALING_STRETCH;
    swapchain.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    CheckHr(m_compositor.Factory()->CreateSwapChainForComposition(m_compositor.Device(), &swapchain, nullptr, &m_swapchain), "Create composition swapchain");
    CheckHr(m_compositor.Composition()->CreateTargetForHwnd(window, TRUE, &m_target), "Create composition target");
    CheckHr(m_compositor.Composition()->CreateVisual(&m_visual), "Create composition visual");
    CheckHr(m_visual->SetContent(m_swapchain.Get()), "Set composition content");
    CheckHr(m_target->SetRoot(m_visual.Get()), "Set composition root");
    CheckHr(m_compositor.Composition()->Commit(), "Commit composition target");
}

bool Win32CompositionSurface::TryPresent(unsigned index) {
    if (!m_copied) {
        auto& frame = m_frames.at(index);
        const auto acquired = frame.mutex->AcquireSync(1, 0);
        if (acquired == WAIT_TIMEOUT) return false;
        CheckHr(acquired, "Acquire shared texture for composition");
        try {
            ComPtr<ID3D11Texture2D> buffer;
            CheckHr(m_swapchain->GetBuffer(0, IID_PPV_ARGS(&buffer)), "Get composition backbuffer");
            m_compositor.Immediate()->CopyResource(buffer.Get(), frame.texture.Get());
            m_compositor.Immediate()->Flush();
        } catch (...) { frame.mutex->ReleaseSync(0); throw; }
        CheckHr(frame.mutex->ReleaseSync(0), "Release composition texture");
        m_copied = true;
    }
    const auto result = m_swapchain->Present(1, DXGI_PRESENT_DO_NOT_WAIT);
    if (result == DXGI_ERROR_WAS_STILL_DRAWING || result == DXGI_STATUS_OCCLUDED) return false;
    CheckHr(result, "Present composition surface");
    m_copied = false;
    return true;
}

Win32CompositionSurface::~Win32CompositionSurface() {
    if (m_target) {
        m_target->SetRoot(nullptr);
        m_compositor.Composition()->Commit();
    }
    m_visual.Reset();
    m_target.Reset();
    m_swapchain.Reset();
    for (auto& frame : m_frames) if (frame.handle) CloseHandle(frame.handle);
}
}

