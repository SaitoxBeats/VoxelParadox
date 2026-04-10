#pragma once

// 1. Project
#include "hud_style_primitives.hpp"

struct SettingsMenuHUDStyle {
    HUDPanelStyle panel{{0.0f, 0.0f, 0.0f, 0.78f}};
    glm::vec3 titleColor{0.95f, 0.98f, 1.0f};
    glm::vec3 subtitleColor{0.70f, 0.78f, 0.90f};
    glm::vec3 sectionTitleColor{0.82f, 0.90f, 1.0f};
    glm::vec3 labelColor{0.78f, 0.86f, 0.98f};
    glm::vec3 valueColor{1.0f, 1.0f, 1.0f};
    glm::vec3 disabledValueColor{0.60f, 0.64f, 0.72f};
    HUDTextButtonStyle button{
        glm::vec3(0.90f, 0.96f, 1.0f),
        glm::vec3(1.0f, 1.0f, 0.2f)
    };
    HUDTextButtonStyle inactiveTabButton{
        glm::vec3(0.72f, 0.82f, 0.94f),
        glm::vec3(1.0f, 1.0f, 0.2f)
    };
    HUDTextButtonStyle activeTabButton{
        glm::vec3(1.0f, 1.0f, 0.34f),
        glm::vec3(1.0f, 1.0f, 0.34f)
    };
    HUDTextButtonStyle footerButton{
        glm::vec3(1.0f, 1.0f, 1.0f),
        glm::vec3(1.0f, 1.0f, 0.2f)
    };
    glm::vec3 warningColor{1.0f, 0.72f, 0.72f};
};

namespace HUDStyles {

inline const SettingsMenuHUDStyle kSettingsMenu{};

}  // namespace HUDStyles
