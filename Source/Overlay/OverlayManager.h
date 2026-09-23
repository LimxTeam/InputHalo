/*******************************************************************************
 * 文件: OverlayManager.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   声明覆盖层管理器 OverlayManager 及其内部目标结构 Target、视图描述 View。
 *   不涉及复杂算法；纯接口与聚合声明。
 *   空间-时间权衡：用 unique_ptr 持有渲染器与合成目标，按需分配；目标向量随监视器数线性增长。
 *
 * 设计哲学:
 *   以 RAII 方式聚合窗口、合成目标与渲染器；构造时绑定 Vulkan 上下文，析构时自动 Shutdown。
 *
 * 技术特性:
 *   - 视图结构：描述显示器边界、DPI 缩放、主屏标志
 *   - 目标结构：聚合覆盖窗口、合成渲染器、显示状态
 *   - 生命周期管理：构造绑定上下文，Initialize 建立资源，析构自动清理
 *   - 交互控制：SetInteractive 批量切换所有窗口的鼠标穿透
 *
 * 依赖关系:
 *   内部依赖：ih::platform::Win32OverlayWindow、ih::render::VulkanCompositionTarget
 *   外部依赖：Vulkan 上下文（VulkanContext）、C++ 标准库 <memory>/<vector>/<functional>
 *
 * 注意事项:
 *   OverlayManager 不可拷贝，通过引用持有 VulkanContext（生命周期由外部保证）
 *   m_visible 默认为 true，首帧呈现后窗口自动显示
 *
 ******************************************************************************/
#pragma once
#include "Platform/Windows/Win32OverlayWindow.h"
#include "RHI/Vulkan/VulkanCompositionTarget.h"
#include <memory>
#include <vector>
#include <functional>

// 覆盖层管理命名空间（顶层 ih）
namespace ih {
// 覆盖层管理器：多显示器透明覆盖的中枢
// 持有 Vulkan 上下文引用、DXGI 合成器、2D 渲染器与每屏目标
class OverlayManager {
public:
    // 构造：绑定 Vulkan 上下文引用（不持有所有权）
    explicit OverlayManager(render::VulkanContext& context) : m_context(context) {}
    // 析构：自动调用 Shutdown 释放资源
    ~OverlayManager() { Shutdown(); }
    // 初始化：传入着色器路径，建立全部渲染资源
    std::expected<void, std::string> Initialize(const std::filesystem::path& shader);
    // 逐帧更新，返回是否有待呈现帧
    std::expected<bool, std::string> Update();
    // 消费显示器变更信号
    bool ConsumeDisplayChange();
    // 请求重绘
    void RequestRedraw();
    // 手动关闭（通常由析构自动调用）
    void Shutdown();
    size_t TargetCount() const { return m_targets.size(); }
    uint64_t PresentedFrames() const;
    // 视图描述：一块显示器的边界、DPI、主屏标志
    struct View { render2d::Rect bounds; float dpi{}; bool primary{}; };
    // 获取所有显示器视图
    std::vector<View> Views() const;
    // 注入场景绘制回调（按屏幕生成 DrawList）
    void SetScenes(const std::function<render2d::DrawList(const View&, size_t)>& paint);
    // 设置整体可见性
    void SetVisible(bool visible);
    // 批量切换所有窗口的鼠标交互（穿透/可点击）
    void SetInteractive(bool enabled){for(auto& target:m_targets)target->window.SetInteractive(enabled);}
private:
    // 内部目标结构：一块显示器对应的窗口+渲染器组合
    // shown 标记窗口是否已实际显示（首帧后才显示）
    struct Target {
        platform::Win32OverlayWindow window;
        std::unique_ptr<render::VulkanCompositionTarget> renderer;
        bool shown{};
        View view;
    };
    // Vulkan 上下文引用（外部持有，不释放）
    render::VulkanContext& m_context;
    // DXGI/DirectComposition 合成器（跨 API 纹理共享核心）
    std::unique_ptr<platform::Win32Compositor> m_compositor;
    // 共享的 Vulkan 2D 渲染器（所有目标复用）
    std::unique_ptr<render::VulkanRenderer2D> m_renderer;
    // 每显示器目标列表
    std::vector<std::unique_ptr<Target>> m_targets;
    // 整体可见性标志
    bool m_visible = true;
};
}

