#pragma once

// 1. External
#include <glm/glm.hpp>

struct HUDPanelStyle {
    glm::vec4 color{0.0f};
};

struct HUDTextButtonStyle {
    glm::vec3 normalColor{1.0f};
    glm::vec3 hoverColor{1.0f};
};
