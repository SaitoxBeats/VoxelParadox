#pragma once

#include <array>
#include <string>

#include <glm/glm.hpp>

#include "hud_element.hpp"

class hudText;

class hudInitialMenu : public HUDElement {
public:
    enum class ActionType {
        None = 0,
        StartGame,
        OpenSettings,
        ExitGame,
    };

    explicit hudInitialMenu(const std::string& fontPath = "");
    ~hudInitialMenu() override;

    void setEnabled(bool enabled);
    ActionType consumeAction();

    void update(int screenWidth, int screenHeight) override;
    void draw(class Shader& shader, int screenWidth, int screenHeight) override;

private:
    struct LayoutMetrics {
        glm::ivec4 panelRect{0};
        glm::ivec4 titleRect{0};
        std::array<glm::ivec4, 3> buttonRects{};
    };

    static constexpr int kButtonCount = 3;

    bool enabled_ = true;
    int selectedButton_ = 0;
    int hoveredButton_ = -1;
    LayoutMetrics layout_{};
    ActionType pendingAction_ = ActionType::None;

    hudText* titleText_ = nullptr;
    hudText* subtitleText_ = nullptr;
    std::array<hudText*, 3> buttonTexts_{};

    void updateLayout(int screenWidth, int screenHeight);
    void activateButton(int index);

    bool pointInRect(float x, float y, const glm::ivec4& rect) const;
    void drawRect(class Shader& shader, const glm::ivec4& rect,
                  const glm::vec4& color) const;
    void drawCenteredText(hudText& text, const std::string& value,
                          const glm::ivec4& rect, int screenWidth,
                          int screenHeight, class Shader& shader) const;
};
