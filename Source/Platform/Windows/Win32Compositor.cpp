/*******************************************************************************
 * 文件: Win32Compositor.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   实现 Vulkan-DXGI-DirectComposition 互操作：创建 D3D11 合成设备、匹配 Vulkan 物理设备 LUID、管理共享纹理与交换链呈现。
 *   算法复杂度：适配器枚举 O(n)，n 为 GPU 数量；呈现路径 O(1)。
 *   空间-时间权衡：双缓冲共享纹理以延迟呈现换帧率；用 KeyedMutex 跨 API 同步避免数据竞争。
 *
 * 设计哲学:
 *   Vulkan 渲染帧通过共享纹理句柄交给 D3D11 复制到交换链后备缓冲，再由 DirectComposition 合成到透明覆盖窗口。
 *
 * 技术特性:
 *   - LUID 匹配：用 Vulkan 设备 LUID 在 DXGI 适配器中定位同一块 GPU
 *   - 共享纹理：创建带 NTHANDLE+KEYEDMUTEX 的 D3D11 纹理，导出共享句柄供 Vulkan 写入
 *   - 非阻塞呈现：TryPresent 用 DO_NOT_WAIT 标志，超时则由调用方重试
 *   - 预乘 Alpha：交换链使用 PREMULTIPLIED 模式，支持透明叠加
 *
 * 依赖关系:
 *   内部依赖：ih::platform::Win32Compositor
 *   外部依赖：D3D11、DXGI 1.2、DirectComposition、WRL ComPtr、Win32 Handle
 *
 * 注意事项:
 *   所有 HRESULT 失败均抛出异常，由上层 catch 回滚
 *   共享纹理句柄在析构时 CloseHandle 关闭
 *
 ******************************************************************************/
#include "Platform/Windows/Win32Compositor.h"
#include <cstring>
#include <stdexcept>
#include <string>

// Win32 合成器命名空间
namespace ih::platform {
// 匿名命名空间：内部 HRESULT 检查工具
namespace {
// 检查 HRESULT，失败时抛出含操作名与错误码的异常
void CheckHr(HRESULT status, const char* operation) {
    if (status != S_OK) throw std::runtime_error(std::string(operation) + " (HRESULT " + std::to_string(static_cast<uint32_t>(status)) + ")");
}
}
// 初始化合成器：创建 DXGI 工厂、匹配适配器、创建 D3D11 与 DComp 设备
// adapterLuid 为 Vulkan 物理设备的 LUID，用于定位同一 GPU
void Win32Compositor::Initialize(const std::array<uint8_t, 8>& adapterLuid) {
    // 创建 DXGI 工厂（IDXGIFactory1），用于枚举适配器与创建交换链
    CheckHr(CreateDXGIFactory1(IID_PPV_ARGS(&m_factory)), "Create DXGI factory");
    ComPtr<IDXGIAdapter1> selected;
    // 遍历所有 DXGI 适配器，查找 LUID 匹配项
    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> adapter;
        // 枚举下一个适配器
        const auto result = m_factory->EnumAdapters1(index, &adapter);
        // 枚举完毕则退出循环
        if (result == DXGI_ERROR_NOT_FOUND) break;
        CheckHr(result, "Enumerate DXGI adapter");
        // 获取适配器描述（含 LUID）
        DXGI_ADAPTER_DESC1 description{};
        CheckHr(adapter->GetDesc1(&description), "DXGI adapter description");
        static_assert(sizeof(description.AdapterLuid) == 8);
        // 比较 LUID：匹配则选中该适配器
        if (std::memcmp(&description.AdapterLuid, adapterLuid.data(), 8) == 0) { selected = adapter; break; }
    }
    // 未找到匹配适配器则报错
    if (!selected) throw std::runtime_error("No DXGI adapter matching the Vulkan device LUID");
    // 创建 D3D11 设备（BGRA 支持）作为合成复制设备
    CheckHr(D3D11CreateDevice(selected.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr, 0, D3D11_SDK_VERSION, &m_device, nullptr, &m_immediate), "Create composition copy device");
    // 获取 D3D 设备的 DXGI 设备接口
    ComPtr<IDXGIDevice> dxgiDevice;
    CheckHr(m_device.As(&dxgiDevice), "Get DXGI device");
    // 创建 DirectComposition 设备，用于合成视觉树到 HWND
    CheckHr(DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&m_composition)), "Create DirectComposition device");
}

// 初始化一块合成表面：创建共享纹理、交换链、视觉树并提交到窗口
// window 为目标覆盖窗口，width/height 为物理像素尺寸
void Win32CompositionSurface::Initialize(HWND window, uint32_t width, uint32_t height) {
    // 配置 D3D11 纹理描述：RGBA8、单层级、默认用法
    D3D11_TEXTURE2D_DESC texture{};
    texture.Width = width;
    texture.Height = height;
    texture.MipLevels = texture.ArraySize = 1;
    texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture.SampleDesc.Count = 1;
    texture.Usage = D3D11_USAGE_DEFAULT;
    texture.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    // 关键：启用 NTHANDLE 共享句柄 + KeyedMutex 跨 API 同步
    texture.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    // 为每帧（双缓冲）创建共享纹理与互斥体
    for (auto& frame : m_frames) {
        // 创建 D3D11 纹理
        CheckHr(m_compositor.Device()->CreateTexture2D(&texture, nullptr, &frame.texture), "Create shared overlay texture");
        // 获取 KeyedMutex 接口用于跨 API 同步
        CheckHr(frame.texture.As(&frame.mutex), "Get shared texture mutex");
        ComPtr<IDXGIResource1> resource;
        CheckHr(frame.texture.As(&resource), "Get shared texture resource");
        // 导出共享纹理句柄，供 Vulkan 设备打开并写入
        CheckHr(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
            nullptr, &frame.handle), "Create shared texture handle");
    }
    // 配置交换链描述：FLIP_SEQUENTIAL、预乘 Alpha、拉伸缩放
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
    // 创建用于合成的交换链（非 HWND 绑定，而是 DComp 视觉内容）
    CheckHr(m_compositor.Factory()->CreateSwapChainForComposition(m_compositor.Device(), &swapchain, nullptr, &m_swapchain), "Create composition swapchain");
    // 为目标 HWND 创建合成目标
    CheckHr(m_compositor.Composition()->CreateTargetForHwnd(window, TRUE, &m_target), "Create composition target");
    // 创建合成视觉节点
    CheckHr(m_compositor.Composition()->CreateVisual(&m_visual), "Create composition visual");
    // 将交换链作为视觉内容
    CheckHr(m_visual->SetContent(m_swapchain.Get()), "Set composition content");
    // 将视觉设为合成树根节点
    CheckHr(m_target->SetRoot(m_visual.Get()), "Set composition root");
    // 提交合成命令，使窗口内容生效
    CheckHr(m_compositor.Composition()->Commit(), "Commit composition target");
}

// 尝试非阻塞呈现指定帧
// 返回 true 表示呈现已排队；false 表示需重试同一帧
bool Win32CompositionSurface::TryPresent(unsigned index) {
    // 若该帧尚未从共享纹理复制到交换链后备缓冲
    if (!m_copied) {
        auto& frame = m_frames.at(index);
        // 获取共享纹理的 KeyedMutex（等待 0ms，超时即放弃）
        const auto acquired = frame.mutex->AcquireSync(1, 0);
        // 超时则返回 false，稍后重试
        if (acquired == WAIT_TIMEOUT) return false;
        CheckHr(acquired, "Acquire shared texture for composition");
        try {
            // 获取交换链后备缓冲纹理
            ComPtr<ID3D11Texture2D> buffer;
            CheckHr(m_swapchain->GetBuffer(0, IID_PPV_ARGS(&buffer)), "Get composition backbuffer");
            // 将 Vulkan 写入的共享纹理复制到后备缓冲
            m_compositor.Immediate()->CopyResource(buffer.Get(), frame.texture.Get());
            // 刷新 D3D11 命令队列，确保复制完成
            m_compositor.Immediate()->Flush();
        // 异常时释放互斥体后重新抛出
        } catch (...) { frame.mutex->ReleaseSync(0); throw; }
        // 释放共享纹理互斥体
        CheckHr(frame.mutex->ReleaseSync(0), "Release composition texture");
        m_copied = true;
    }
    // 非阻塞呈现交换链（1 帧同步间隔，DO_NOT_WAIT）
    const auto result = m_swapchain->Present(1, DXGI_PRESENT_DO_NOT_WAIT);
    // 仍在绘制或被遮挡时返回 false，等待下一次重试
    if (result == DXGI_ERROR_WAS_STILL_DRAWING || result == DXGI_STATUS_OCCLUDED) return false;
    CheckHr(result, "Present composition surface");
    m_copied = false;
    return true;
}

// 析构：解绑视觉树、重置 COM 对象、关闭共享纹理句柄
Win32CompositionSurface::~Win32CompositionSurface() {
    if (m_target) {
        // 清除合成根视觉
        m_target->SetRoot(nullptr);
        // 提交使解绑生效
        m_compositor.Composition()->Commit();
    }
    m_visual.Reset();
    m_target.Reset();
    m_swapchain.Reset();
    // 关闭所有共享纹理句柄
    for (auto& frame : m_frames) if (frame.handle) CloseHandle(frame.handle);
}
}

