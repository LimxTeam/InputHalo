#pragma once
#include <Volk/volk.h>
namespace ih::render {
struct VulkanFrame {
    VkCommandPool pool{};
    VkCommandBuffer command{};
    VkSemaphore acquired{};
    VkFence finished{};
};
inline constexpr unsigned kFramesInFlight = 2;
}
