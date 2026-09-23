#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>

namespace ih::platform {
inline constexpr unsigned kCompositionBufferCount = 2;
template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

class Win32Compositor {
public:
    void Initialize(const std::array<uint8_t, 8>& adapterLuid);
    ID3D11Device* Device() const { return m_device.Get(); }
    ID3D11DeviceContext* Immediate() const { return m_immediate.Get(); }
    IDXGIFactory2* Factory() const { return m_factory.Get(); }
    IDCompositionDevice* Composition() const { return m_composition.Get(); }
private:
    ComPtr<IDXGIFactory2> m_factory;
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_immediate;
    ComPtr<IDCompositionDevice> m_composition;
};

class Win32CompositionSurface {
public:
    explicit Win32CompositionSurface(Win32Compositor& compositor) : m_compositor(compositor) {}
    ~Win32CompositionSurface();
    Win32CompositionSurface(const Win32CompositionSurface&) = delete;
    Win32CompositionSurface& operator=(const Win32CompositionSurface&) = delete;
    void Initialize(HWND window, uint32_t width, uint32_t height);
    HANDLE SharedHandle(unsigned index) const { return m_frames.at(index).handle; }
    // Nonblocking. Caller retries the same frame while presentation is pending.
    bool TryPresent(unsigned index);
private:
    struct Frame {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<IDXGIKeyedMutex> mutex;
        HANDLE handle{};
    };
    Win32Compositor& m_compositor;
    std::array<Frame, kCompositionBufferCount> m_frames;
    ComPtr<IDXGISwapChain1> m_swapchain;
    ComPtr<IDCompositionTarget> m_target;
    ComPtr<IDCompositionVisual> m_visual;
    bool m_copied{};
};
}
