#pragma once
#include "Platform/Windows/Win32OverlayWindow.h"
#include "RHI/Vulkan/VulkanCompositionTarget.h"
#include <memory>
#include <vector>
#include <functional>

namespace ih {
class OverlayManager {
public:
    explicit OverlayManager(render::VulkanContext& context) : m_context(context) {}
    ~OverlayManager() { Shutdown(); }
    std::expected<void, std::string> Initialize(const std::filesystem::path& shader);
    std::expected<bool, std::string> Update();
    bool ConsumeDisplayChange();
    void RequestRedraw();
    void Shutdown();
    size_t TargetCount() const { return m_targets.size(); }
    uint64_t PresentedFrames() const;
    struct View { render2d::Rect bounds; float dpi{}; bool primary{}; };
    std::vector<View> Views() const;
    void SetScenes(const std::function<render2d::DrawList(const View&, size_t)>& paint);
    void SetVisible(bool visible);
    void SetInteractive(bool enabled){for(auto& target:m_targets)target->window.SetInteractive(enabled);}
private:
    struct Target {
        platform::Win32OverlayWindow window;
        std::unique_ptr<render::VulkanCompositionTarget> renderer;
        bool shown{};
        View view;
    };
    render::VulkanContext& m_context;
    std::unique_ptr<platform::Win32Compositor> m_compositor;
    std::unique_ptr<render::VulkanRenderer2D> m_renderer;
    std::vector<std::unique_ptr<Target>> m_targets;
    bool m_visible = true;
};
}

