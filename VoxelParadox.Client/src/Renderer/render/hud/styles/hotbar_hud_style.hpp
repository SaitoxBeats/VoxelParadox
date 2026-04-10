#pragma once

// 1. Project
#include "render/hud/hud_slider.hpp"

struct HotbarHUDStyle {
    glm::vec4 barBorderColor{0.02f, 0.02f, 0.04f, 0.70f};
    glm::vec4 barFillColor{0.06f, 0.06f, 0.10f, 0.55f};
    glm::vec4 slotBorderColor{0.20f, 0.24f, 0.32f, 0.78f};
    glm::vec4 slotFillColor{0.05f, 0.05f, 0.08f, 0.36f};
    glm::vec4 selectionBorderColor{0.95f, 0.97f, 1.0f, 0.90f};
    glm::vec4 selectionFillColor{0.18f, 0.20f, 0.30f, 0.28f};
    glm::vec3 countTextColor{0.88f, 0.90f, 0.95f};
    glm::vec3 selectedCountTextColor{1.0f, 1.0f, 1.0f};
    HudSliderStyle lifeBarStyle{
        glm::vec4(0.02f, 0.02f, 0.04f, 0.70f), //border
        glm::vec4(0.06f, 0.06f, 0.10f, 0.55f),
        glm::vec4(1.77f, 0.00f, 0.03f, 1.00f) //fill
    };
    glm::vec3 portalCooldownTextColor{0.78f, 0.84f, 0.94f};
    glm::vec3 portalReadyTextColor{1.0f, 0.94f, 0.58f};
    glm::vec3 portalSandboxTextColor{0.55f, 1.0f, 0.68f};
};

namespace HUDStyles {

inline const HotbarHUDStyle kHotbar{};

}  // namespace HUDStyles
