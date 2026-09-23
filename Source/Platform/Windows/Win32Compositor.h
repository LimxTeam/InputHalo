/*******************************************************************************
 * 文件: Win32Compositor.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   声明合成器 Win32Compositor 与合成表面 Win32CompositionSurface，封装 D3D11/DXGI/DComp 互操作。
 *   不涉及复杂算法；接口与 COM 成员声明。
 *   空间-时间权衡：双缓冲（kCompositionBufferCount=2）共享纹理，在延迟与吞吐间平衡。
 *
 * 设计哲学:
 *   ComPtr 自动管理 COM 生命周期；合成表面持有共享句柄供 Vulkan 侧打开。
 *
 * 技术特性:
 *   - 合成器类：持有 DXGI 工厂、D3D11 设备、DComp 设备
 *   - 合成表面类：管理共享纹理数组、交换链、视觉树
 *   - 帧结构：封装共享纹理、KeyedMutex、共享句柄
 *   - 不可拷贝：表面类删除拷贝构造与赋值
 *
 * 依赖关系:
 *   外部依赖：D3D11、DXGI 1.2、DirectComposition、WRL ComPtr、Win32 API
 *
 * 注意事项:
 *   ComPtr 别名简化 WRL 模板书写
 *   TryPresent 为非阻塞，调用方需重试
 *
 ******************************************************************************/
#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>

// Win32 合成器命名空间
namespace ih::platform {
// 合成缓冲数量：双缓冲，减少呈现阻塞
inline constexpr unsigned kCompositionBufferCount = 2;
// ComPtr 别名：简化 Microsoft::WRL::ComPtr 书写
template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

// 合成器：全局持有 DXGI/D3D11/DComp 设备
// 所有合成表面共享同一组合成器实例
class Win32Compositor {
public:
    // 初始化：按 Vulkan LUID 匹配并创建设备
    void Initialize(const std::array<uint8_t, 8>& adapterLuid);
    // 获取 D3D11 设备指针（供表面创建纹理）
    ID3D11Device* Device() const { return m_device.Get(); }
    // 获取 D3D11 立即上下文（供复制操作）
    ID3D11DeviceContext* Immediate() const { return m_immediate.Get(); }
    // 获取 DXGI 工厂指针（供创建交换链）
    IDXGIFactory2* Factory() const { return m_factory.Get(); }
    // 获取 DirectComposition 设备指针
    IDCompositionDevice* Composition() const { return m_composition.Get(); }
private:
    ComPtr<IDXGIFactory2> m_factory;
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_immediate;
    ComPtr<IDCompositionDevice> m_composition;
};

// 合成表面：绑定到一个 HWND 的透明覆盖合成目标
// 持有共享纹理句柄供 Vulkan 写入，再复制到 DComp 交换链
class Win32CompositionSurface {
public:
    // 构造：绑定到共享合成器
    explicit Win32CompositionSurface(Win32Compositor& compositor) : m_compositor(compositor) {}
    // 析构：解绑视觉树并释放资源
    ~Win32CompositionSurface();
    Win32CompositionSurface(const Win32CompositionSurface&) = delete;
    Win32CompositionSurface& operator=(const Win32CompositionSurface&) = delete;
    // 初始化：创建共享纹理、交换链、视觉树
    void Initialize(HWND window, uint32_t width, uint32_t height);
    // 获取指定帧的共享纹理句柄（供 Vulkan 打开）
    HANDLE SharedHandle(unsigned index) const { return m_frames.at(index).handle; }
    // 非阻塞呈现注释：调用方在呈现挂起时重试同一帧
    // Nonblocking. Caller retries the same frame while presentation is pending.
    bool TryPresent(unsigned index);
private:
    // 帧结构：一帧共享纹理资源
    struct Frame {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<IDXGIKeyedMutex> mutex;
        HANDLE handle{};
    };
    // 所属合成器引用（不持有所有权）
    Win32Compositor& m_compositor;
    // 双缓冲帧数组
    std::array<Frame, kCompositionBufferCount> m_frames;
    // DComp 交换链
    ComPtr<IDXGISwapChain1> m_swapchain;
    // DComp 目标（绑定到 HWND）
    ComPtr<IDCompositionTarget> m_target;
    // DComp 视觉节点
    ComPtr<IDCompositionVisual> m_visual;
    // 标记本帧是否已从共享纹理复制到后备缓冲
    bool m_copied{};
};
}
