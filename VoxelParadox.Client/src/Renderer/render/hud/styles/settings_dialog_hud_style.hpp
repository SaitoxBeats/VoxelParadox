#pragma once

// 1. Project
#include "hud_style_primitives.hpp"

struct SettingsDiscardConfirmHUDStyle {
    HUDPanelStyle panel{{0.0f, 0.0f, 0.0f, 0.82f}};
    glm::vec3 titleColor{0.95f, 0.98f, 1.0f};
    glm::vec3 messageColor{0.76f, 0.84f, 0.96f};
    HUDTextButtonStyle button{
        glm::vec3(0.90f, 0.96f, 1.0f),
        glm::vec3(1.0f, 1.0f, 0.2f)
    };
};

struct SettingsControlsCaptureHUDStyle {
    HUDPanelStyle panel{{0.0f, 0.0f, 0.0f, 0.84f}};
    glm::vec3 titleColor{0.95f, 0.98f, 1.0f};
    glm::vec3 actionLabelColor{0.90f, 0.96f, 1.0f};
    glm::vec3 promptColor{0.78f, 0.86f, 0.98f};
};

namespace HUDStyles {

inline const SettingsDiscardConfirmHUDStyle kSettingsDiscardConfirm{};
inline const SettingsControlsCaptureHUDStyle kSettingsControlsCapture{};

}  // namespace HUDStyles
