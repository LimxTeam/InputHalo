#include "Overlay/OverlayManager.h"
#include "Platform/Windows/Win32Monitor.h"
#include "Render2D/Painter.h"
#include <algorithm>
#include <stdexcept>

namespace ih {
std::expected<void, std::string> OverlayManager::Initialize(const std::filesystem::path& shader) {
    Shutdown();
    try {
        if (!m_context.CompositionSupported()) throw std::runtime_error("Vulkan device does not support Windows composition interop");
        VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties.pNext = &id;
        vkGetPhysicalDeviceProperties2(m_context.PhysicalDevice(), &properties);
        std::array<uint8_t, 8> luid{};
        std::copy_n(id.deviceLUID, luid.size(), luid.begin());
        m_compositor = std::make_unique<platform::Win32Compositor>();
        m_compositor->Initialize(luid);
        m_renderer = std::make_unique<render::VulkanRenderer2D>(m_context);
        m_renderer->Initialize(shader);
        const auto monitors = platform::EnumerateMonitors();
        if (!monitors) throw std::runtime_error(monitors.error());
        for (const auto& monitor : *monitors) {
            auto target = std::make_unique<Target>();
            auto created = target->window.Create(monitor.bounds);
            if (!created) throw std::runtime_error(created.error());
            MONITORINFO monitorInfo{sizeof(MONITORINFO)};
            GetMonitorInfoW(monitor.handle, &monitorInfo);
            target->view = {{static_cast<float>(monitor.bounds.left),static_cast<float>(monitor.bounds.top),
                static_cast<float>(monitor.bounds.right-monitor.bounds.left),static_cast<float>(monitor.bounds.bottom-monitor.bounds.top)},
                target->window.DpiScale(),(monitorInfo.dwFlags & MONITORINFOF_PRIMARY)!=0};
            target->renderer = std::make_unique<render::VulkanCompositionTarget>(m_context, *m_renderer, *m_compositor);
            target->renderer->Initialize(target->window.Handle(), monitor.bounds.right - monitor.bounds.left,
                monitor.bounds.bottom - monitor.bounds.top, target->window.DpiScale());
            const float width = (monitor.bounds.right - monitor.bounds.left) / target->window.DpiScale();
            const float height = (monitor.bounds.bottom - monitor.bounds.top) / target->window.DpiScale();
            render2d::Painter painter(width, height);
            painter.Circle({width * .5f, height * .5f}, 48, {117.f / 255, 230.f / 255, 200.f / 255, .85f});
            target->renderer->SetDrawList(painter.List());
            m_targets.push_back(std::move(target));
        }
        return {};
    } catch (const std::exception& error) {
        Shutdown();
        return std::unexpected(error.what());
    }
}
std::expected<bool, std::string> OverlayManager::Update() {
    try {
        bool pending = false;
        for (auto& target : m_targets) {
            pending = target->renderer->Update() || pending;
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
bool OverlayManager::ConsumeDisplayChange() {
    bool changed = false;
    for (auto& target : m_targets) changed = target->window.ConsumeDisplayChange() || changed;
    return changed;
}
void OverlayManager::RequestRedraw() {
    for (auto& target : m_targets) target->renderer->RequestRedraw();
}
std::vector<OverlayManager::View> OverlayManager::Views() const {
    std::vector<View> views;
    for(const auto& target:m_targets) views.push_back(target->view);
    return views;
}
void OverlayManager::SetScenes(const std::function<render2d::DrawList(const View&, size_t)>& paint) {
    for(size_t i=0;i<m_targets.size();++i) m_targets[i]->renderer->SetDrawList(paint(m_targets[i]->view,i));
}
void OverlayManager::SetVisible(bool visible) {
    m_visible=visible;
    if(!visible) for(auto& target:m_targets) {target->window.Hide();target->shown=false;}
}
uint64_t OverlayManager::PresentedFrames() const {
    uint64_t count = 0;
    for (const auto& target : m_targets) count += target->renderer->PresentedFrames();
    return count;
}
void OverlayManager::Shutdown() {
    for (auto& target : m_targets) target->window.Hide();
    m_targets.clear();
    m_renderer.reset();
    m_compositor.reset();
}
}

