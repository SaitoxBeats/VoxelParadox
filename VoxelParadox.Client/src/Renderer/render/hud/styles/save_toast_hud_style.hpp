#pragma once

// 1. External
#include <glm/glm.hpp>

struct SaveToastHUDStyle {
    glm::vec3 textColor{0.80f, 1.0f, 0.86f};
};

namespace HUDStyles {

inline const SaveToastHUDStyle kSaveToast{};

}  // namespace HUDStyles
