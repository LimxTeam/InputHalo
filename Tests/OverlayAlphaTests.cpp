#include "../Tools/Experiments/TransparentOverlay/AlphaPolicy.h"
#include <iostream>

int main() {
    using ih::experiment::SelectOverlayAlpha;
    // A driver reporting only OPAQUE is a compatibility failure, not a black fullscreen overlay.
    if (SelectOverlayAlpha(VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) || SelectOverlayAlpha(0)) {
        std::cerr << "Non-transparent surface was accepted\n";
        return 1;
    }
    const auto all = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR | VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR |
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR | VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    if (SelectOverlayAlpha(all) != VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR ||
        SelectOverlayAlpha(VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR | VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) != VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR ||
        SelectOverlayAlpha(VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR | VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) != VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) {
        std::cerr << "Overlay alpha preference does not match specification section 14\n";
        return 1;
    }
    std::cout << "Overlay alpha compatibility policy passed\n";
    return 0;
}
