#pragma once
#include <vulkan/vulkan_core.h>
#include <initializer_list>
#include <optional>

namespace ih::experiment {
inline std::optional<VkCompositeAlphaFlagBitsKHR> SelectOverlayAlpha(VkCompositeAlphaFlagsKHR supported) {
    for (const auto mode : {VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) {
        if (supported & mode) return mode;
    }
    // OPAQUE must never be silently selected for a transparent overlay.
    return std::nullopt;
}
}
