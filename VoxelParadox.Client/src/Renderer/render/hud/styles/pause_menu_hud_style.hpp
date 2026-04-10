#pragma once

// 1. Project
#include "hud_style_primitives.hpp"

struct PauseMenuHUDStyle {
    HUDPanelStyle panel{{0.0f, 0.0f, 0.0f, 0.65f}};
    HUDTextButtonStyle continueButton{
        glm::vec3(1.0f),
        glm::vec3(1.0f, 1.0f, 0.2f)
    };
    HUDTextButtonStyle settingsButton{
        glm::vec3(0.8f, 0.95f, 1.0f),
        glm::vec3(1.0f, 1.0f, 0.2f)
    };
    HUDTextButtonStyle exitButton{
        glm::vec3(1.0f),
        glm::vec3(1.0f, 0.4f, 0.4f)
    };
};

namespace HUDStyles {

inline const PauseMenuHUDStyle kPauseMenu{};

}  // namespace HUDStyles
