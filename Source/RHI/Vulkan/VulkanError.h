#pragma once
#include <Volk/volk.h>
#include <stdexcept>
#include <string>

namespace ih::render {
inline void CheckVk(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed (VkResult " +
            std::to_string(result) + (result == VK_ERROR_DEVICE_LOST ? ", device lost)" : ")"));
}
}
