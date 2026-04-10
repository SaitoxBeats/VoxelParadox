// 1. Standard
#include "hud_slider.hpp"

#include <algorithm>
#include <cmath>

// 2. External
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

// 3. Project
#include "hud.hpp"

hudSlider::hudSlider(
    const HUDLayout& layout,
    glm::vec2 size,
    ValueBinder valueBinder,
    HudSliderStyle style,
    int borderThickness,
    int fillInset,
    VisibleBinder visible
)
    : layoutSpec_(layout),
      size_(size),
      valueBinder_(std::move(valueBinder)),
      visibleBinder_(std::move(visible)),
      style_(style),
      borderThickness_(std::max(0, borderThickness)),
      fillInset_(std::max(0, fillInset)) {}

void hudSlider::setStyleBinder(StyleBinder newStyleBinder) {
    styleBinder_ = std::move(newStyleBinder);
}

void hudSlider::drawRect(
    Shader& shader,
    const glm::ivec4& rect,
    const glm::vec4& color
) const {
    if (rect.z <= 0 || rect.w <= 0) {
        return;
    }

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(
        model,
        glm::vec3(static_cast<float>(rect.x), static_cast<float>(rect.y), 0.0f)
    );
    model = glm::scale(
        model,
        glm::vec3(static_cast<float>(rect.z), static_cast<float>(rect.w), 1.0f)
    );

    shader.setMat4("model", model);
    shader.setInt("isText", 0);
    shader.setVec4("color", color);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, HUD::getWhiteTexture());

    HUD::bindQuad();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    HUD::unbindQuad();
}

void hudSlider::draw(Shader& shader, int screenWidth, int screenHeight) {
    if (visibleBinder_ && !visibleBinder_()) {
        return;
    }
    if (!valueBinder_) {
        return;
    }

    const HudSliderStyle currentStyle =
        styleBinder_ ? styleBinder_() : style_;

    const glm::vec2 drawPosition =
        resolveHUDPosition(layoutSpec_, screenWidth, screenHeight, size_);
    const glm::ivec4 sliderRect(
        static_cast<int>(std::round(drawPosition.x)),
        static_cast<int>(std::round(drawPosition.y)),
        static_cast<int>(std::round(size_.x)),
        static_cast<int>(std::round(size_.y))
    );

    drawRect(
        shader,
        glm::ivec4(
            sliderRect.x - borderThickness_,
            sliderRect.y - borderThickness_,
            sliderRect.z + borderThickness_ * 2,
            sliderRect.w + borderThickness_ * 2
        ),
        currentStyle.borderColor
    );
    drawRect(shader, sliderRect, currentStyle.trackColor);

    const glm::ivec4 fillBounds(
        sliderRect.x + fillInset_,
        sliderRect.y + fillInset_,
        std::max(0, sliderRect.z - fillInset_ * 2),
        std::max(0, sliderRect.w - fillInset_ * 2)
    );
    if (fillBounds.z <= 0 || fillBounds.w <= 0) {
        return;
    }

    const float normalizedValue = glm::clamp(valueBinder_(), 0.0f, 1.0f);
    const int fillWidth =
        normalizedValue >= 1.0f
            ? fillBounds.z
            : static_cast<int>(std::round(
                  static_cast<float>(fillBounds.z) * normalizedValue
              ));
    if (fillWidth <= 0) {
        return;
    }

    drawRect(
        shader,
        glm::ivec4(fillBounds.x, fillBounds.y, fillWidth, fillBounds.w),
        currentStyle.fillColor
    );
}
