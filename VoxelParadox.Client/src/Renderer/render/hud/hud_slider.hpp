// 1. Standard
#pragma once

#include <functional>

// 2. External
#include <glm/glm.hpp>

// 3. Project
#include "hud_element.hpp"

struct HudSliderStyle {
    glm::vec4 borderColor   { 0.02f, 0.02f, 0.04f, 0.70f };
    glm::vec4 trackColor    { 0.04f, 0.05f, 0.08f, 0.82f };
    glm::vec4 fillColor     { 1.0f, 0.22f, 0.22f, 0.96f  };
};

class hudSlider : public HUDElement {
public:
    using ValueBinder = std::function<float()>;
    using StyleBinder = std::function<HudSliderStyle()>;
    using VisibleBinder = std::function<bool()>;

    hudSlider(
        const HUDLayout& layout,
        glm::vec2 size,
        ValueBinder valueBinder,
        HudSliderStyle style = {},
        int borderThickness = 2,
        int fillInset = 2,
        VisibleBinder visible = {}
    );

    void setStyleBinder(StyleBinder newStyleBinder);
    void draw(class Shader& shader, int screenWidth, int screenHeight) override;

private:
    HUDLayout layoutSpec_{};
    glm::vec2 size_{0.0f};
    ValueBinder valueBinder_{};
    StyleBinder styleBinder_{};
    VisibleBinder visibleBinder_{};
    HudSliderStyle style_{};
    int borderThickness_ = 2;
    int fillInset_ = 2;

    void drawRect(
        class Shader& shader,
        const glm::ivec4& rect,
        const glm::vec4& color
    ) const;
};
