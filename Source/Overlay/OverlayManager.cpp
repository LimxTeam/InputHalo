/*******************************************************************************
 * 文件: OverlayManager.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   实现覆盖层管理器：初始化 Vulkan 合成管线、为每个监视器创建透明覆盖窗口与渲染目标、逐帧更新与呈现。
 *   算法复杂度：多显示器目标遍历 O(n)，n 为监视器数（通常 1-3），每帧开销恒定。
 *   空间-时间权衡：每显示器独立 VulkanCompositionTarget，以显存换独立性；共享一个 VulkanRenderer2D 避免重复编译着色器。
 *
 * 设计哲学:
 *   管理器是 Overlay 层中枢：聚合 Win32 覆盖窗口、Vulkan 合成目标与 2D 渲染器，对上层暴露简洁的 Update/SetScenes 接口。
 *
 * 技术特性:
 *   - 多屏支持：枚举所有监视器并为每块屏幕创建全屏透明覆盖窗口
 *   - Vulkan-DXGI 互操作：通过 LUID 匹配 Vulkan 物理设备与 DXGI 适配器，实现跨 API 纹理共享
 *   - 延迟显示：首帧成功呈现后才显示窗口，避免黑屏闪烁
 *   - 场景注入：接受 paint 回调为每块屏幕生成 DrawList
 *
 * 依赖关系:
 *   内部依赖：ih::platform（Win32Monitor、Win32Compositor）、ih::render（VulkanRenderer2D、VulkanCompositionTarget）
 *   外部依赖：Vulkan 1.4（vkGetPhysicalDeviceProperties2）、Win32 API（GetMonitorInfoW）、C++ 标准库
 *
 * 注意事项:
 *   Initialize 失败时自动 Shutdown 回滚，保证资源不泄漏
 *   覆盖窗口创建后先用 Painter 画一个占位圆形，确保首帧有内容
 *
 ******************************************************************************/
#include "Overlay/OverlayManager.h"
#include "Platform/Windows/Win32Monitor.h"
#include "Render2D/Painter.h"
#include <algorithm>
#include <stdexcept>

// 覆盖层管理命名空间
namespace ih {
// 初始化覆盖层管理器
// shader 为 2D 渲染管线所需的着色器文件路径
// 返回 std::expected：成功返回 void，失败返回错误描述字符串
std::expected<void, std::string> OverlayManager::Initialize(const std::filesystem::path& shader) {
    // 先清理已有资源，保证幂等初始化
    Shutdown();
    try {
        // 检查 Vulkan 设备是否支持 Windows 合成互操作（需 VK_KHR_external_memory 等扩展）
        if (!m_context.CompositionSupported()) throw std::runtime_error("Vulkan device does not support Windows composition interop");
        // 查询 Vulkan 物理设备的 LUID（本地唯一标识符），用于匹配 DXGI 适配器
        VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties.pNext = &id;
        // 调用 Vulkan API 获取物理设备属性（pNext 链携带 LUID）
        vkGetPhysicalDeviceProperties2(m_context.PhysicalDevice(), &properties);
        // 提取 8 字节 LUID，供 Win32Compositor 匹配同一块 GPU
        std::array<uint8_t, 8> luid{};
        std::copy_n(id.deviceLUID, luid.size(), luid.begin());
        // 创建 DirectComposition 合成器（D3D11 + DXGI + DComp）
        m_compositor = std::make_unique<platform::Win32Compositor>();
        m_compositor->Initialize(luid);
        // 创建共享的 Vulkan 2D 渲染器（编译着色器、建立管线）
        m_renderer = std::make_unique<render::VulkanRenderer2D>(m_context);
        m_renderer->Initialize(shader);
        // 枚举系统所有显示监视器
        const auto monitors = platform::EnumerateMonitors();
        if (!monitors) throw std::runtime_error(monitors.error());
        // 为每块监视器创建一个覆盖目标
        for (const auto& monitor : *monitors) {
            auto target = std::make_unique<Target>();
            // 在监视器边界内创建透明覆盖窗口
            auto created = target->window.Create(monitor.bounds);
            if (!created) throw std::runtime_error(created.error());
            // 查询监视器详细信息（工作区、是否主屏）
            MONITORINFO monitorInfo{sizeof(MONITORINFO)};
            GetMonitorInfoW(monitor.handle, &monitorInfo);
            // 构建视图描述：边界矩形、DPI 缩放、是否为主显示器
            target->view = {{static_cast<float>(monitor.bounds.left),static_cast<float>(monitor.bounds.top),
                static_cast<float>(monitor.bounds.right-monitor.bounds.left),static_cast<float>(monitor.bounds.bottom-monitor.bounds.top)},
                target->window.DpiScale(),(monitorInfo.dwFlags & MONITORINFOF_PRIMARY)!=0};
            // 为该窗口创建 Vulkan 合成目标（交换链、共享纹理）
            target->renderer = std::make_unique<render::VulkanCompositionTarget>(m_context, *m_renderer, *m_compositor);
            target->renderer->Initialize(target->window.Handle(), monitor.bounds.right - monitor.bounds.left,
                monitor.bounds.bottom - monitor.bounds.top, target->window.DpiScale());
            // 计算逻辑像素尺寸（除以 DPI 缩放），用于 2D 绘制坐标
            const float width = (monitor.bounds.right - monitor.bounds.left) / target->window.DpiScale();
            const float height = (monitor.bounds.bottom - monitor.bounds.top) / target->window.DpiScale();
            // 用 Painter 绘制占位圆形，确保首帧有内容可呈现
            render2d::Painter painter(width, height);
            painter.Circle({width * .5f, height * .5f}, 48, {117.f / 255, 230.f / 255, 200.f / 255, .85f});
            // 将绘制列表提交给渲染目标
            target->renderer->SetDrawList(painter.List());
            m_targets.push_back(std::move(target));
        }
        return {};
    // 异常处理：回滚已初始化资源并返回错误
    } catch (const std::exception& error) {
        Shutdown();
        return std::unexpected(error.what());
    }
}
// 逐帧更新覆盖层
// 返回是否有待处理的呈现（true 表示需继续 pump 消息）
std::expected<bool, std::string> OverlayManager::Update() {
    try {
        bool pending = false;
        // 遍历所有显示器目标，推进渲染与呈现
        for (auto& target : m_targets) {
            pending = target->renderer->Update() || pending;
            // 窗口可见且已呈现首帧后，才真正显示窗口（避免闪烁）
            if (m_visible && !target->shown && target->renderer->PresentedFrames()) {
                target->window.Show();
                target->shown = true;
            }
        }
        return pending;
    } catch (const std::exception& error) {
        Shutdown();
        return std::unexpected(error.what());
    }
}
// 消费显示器变更信号（分辨率/DPI/布局变化），返回是否发生变化
bool OverlayManager::ConsumeDisplayChange() {
    bool changed = false;
    for (auto& target : m_targets) changed = target->window.ConsumeDisplayChange() || changed;
    return changed;
}
// 请求所有覆盖目标重绘一帧
void OverlayManager::RequestRedraw() {
    for (auto& target : m_targets) target->renderer->RequestRedraw();
}
// 获取所有显示器的视图描述（边界、DPI、主屏标志）
std::vector<OverlayManager::View> OverlayManager::Views() const {
    std::vector<View> views;
    for(const auto& target:m_targets) views.push_back(target->view);
    return views;
}
// 注入场景绘制回调
// paint 接收视图描述与目标索引，返回该屏幕的 DrawList
void OverlayManager::SetScenes(const std::function<render2d::DrawList(const View&, size_t)>& paint) {
    for(size_t i=0;i<m_targets.size();++i) m_targets[i]->renderer->SetDrawList(paint(m_targets[i]->view,i));
}
// 设置覆盖层整体可见性；隐藏时立即隐藏所有窗口
void OverlayManager::SetVisible(bool visible) {
    m_visible=visible;
    if(!visible) for(auto& target:m_targets) {target->window.Hide();target->shown=false;}
}
// 累计所有目标已呈现的帧数
uint64_t OverlayManager::PresentedFrames() const {
    uint64_t count = 0;
    for (const auto& target : m_targets) count += target->renderer->PresentedFrames();
    return count;
}
// 关闭并释放全部资源
// 隐藏窗口、清空目标、释放渲染器与合成器
void OverlayManager::Shutdown() {
    for (auto& target : m_targets) target->window.Hide();
    m_targets.clear();
    m_renderer.reset();
    m_compositor.reset();
}
}

