#include "hud_initial_menu.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include <GLFW/glfw3.h>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/input.hpp"
#include "hud.hpp"
#include "hud_text.hpp"
#include "input/input_action_ids.hpp"
#include "input/input_action_system.hpp"

namespace {

constexpr float kPanelWidthRatio = 0.34f;
constexpr float kPanelHeightRatio = 0.52f;
constexpr float kPanelMinWidth = 460.0f;
constexpr float kPanelMaxWidth = 620.0f;
constexpr float kPanelMinHeight = 410.0f;
constexpr float kPanelMaxHeight = 520.0f;
constexpr int kOuterPadding = 26;
constexpr float kButtonHeight = 54.0f;
constexpr float kButtonGap = 16.0f;

float clampFloat(float value, float minValue, float maxValue) {
    return std::max(minValue, std::min(value, maxValue));
}

glm::ivec4 expandRect(const glm::ivec4& rect, int amount) {
    return glm::ivec4(
        rect.x - amount,
        rect.y - amount,
        rect.z + amount * 2,
        rect.w + amount * 2
    );
}

} // namespace

hudInitialMenu::hudInitialMenu(const std::string& fontPath) {
    titleText_ = new hudText("Voxel Paradox", 0, 0, glm::vec2(1.0f), 42, fontPath);
    subtitleText_ = new hudText("Your universe is nothing but a dream.", 0, 0, glm::vec2(1.0f), 17, fontPath);
    buttonTexts_[0] = new hudText("Start Game", 0, 0, glm::vec2(1.0f), 22, fontPath);
    buttonTexts_[1] = new hudText("Settings", 0, 0, glm::vec2(1.0f), 22, fontPath);
    buttonTexts_[2] = new hudText("Exit Game", 0, 0, glm::vec2(1.0f), 22, fontPath);

    titleText_->setColor(glm::vec3(0.97f, 0.98f, 1.0f));
    subtitleText_->setColor(glm::vec3(0.68f, 0.72f, 0.80f));
    for (hudText* buttonText : buttonTexts_) {
        if (buttonText) {
            buttonText->setColor(glm::vec3(0.95f, 0.97f, 1.0f));
        }
    }
}

hudInitialMenu::~hudInitialMenu() {
    delete titleText_;
    delete subtitleText_;
    for (hudText*& buttonText : buttonTexts_) {
        delete buttonText;
        buttonText = nullptr;
    }
}

void hudInitialMenu::setEnabled(bool enabled) {
    enabled_ = enabled;
}

hudInitialMenu::ActionType hudInitialMenu::consumeAction() {
    const ActionType action = pendingAction_;
    pendingAction_ = ActionType::None;
    return action;
}

void hudInitialMenu::updateLayout(int screenWidth, int screenHeight) {
    const float panelWidth =
        clampFloat(screenWidth * kPanelWidthRatio, kPanelMinWidth, kPanelMaxWidth);
    const float panelHeight =
        clampFloat(screenHeight * kPanelHeightRatio, kPanelMinHeight, kPanelMaxHeight);

    const int panelX = static_cast<int>(std::round((screenWidth - panelWidth) * 0.5f));
    const int panelY = static_cast<int>(std::round((screenHeight - panelHeight) * 0.5f));
    const int panelW = static_cast<int>(std::round(panelWidth));
    const int panelH = static_cast<int>(std::round(panelHeight));

    layout_.panelRect = glm::ivec4(panelX, panelY, panelW, panelH);
    layout_.titleRect = glm::ivec4(
        panelX + kOuterPadding,
        panelY + kOuterPadding + 6,
        panelW - kOuterPadding * 2,
        86
    );

    const int buttonWidth = panelW - kOuterPadding * 2;
    const int buttonHeight = static_cast<int>(std::round(kButtonHeight));
    const int stackHeight =
        buttonHeight * kButtonCount +
        static_cast<int>(std::round(kButtonGap)) * (kButtonCount - 1);
    const int firstButtonY = panelY + panelH - kOuterPadding - stackHeight;

    for (int index = 0; index < kButtonCount; ++index) {
        layout_.buttonRects[static_cast<std::size_t>(index)] = glm::ivec4(
            panelX + kOuterPadding,
            firstButtonY + static_cast<int>(std::round(index * (kButtonHeight + kButtonGap))),
            buttonWidth,
            buttonHeight
        );
    }
}

bool hudInitialMenu::pointInRect(float x, float y, const glm::ivec4& rect) const {
    return x >= static_cast<float>(rect.x) &&
        y >= static_cast<float>(rect.y) &&
        x <= static_cast<float>(rect.x + rect.z) &&
        y <= static_cast<float>(rect.y + rect.w);
}

void hudInitialMenu::activateButton(int index) {
    switch (index) {
    case 0:
        pendingAction_ = ActionType::StartGame;
        break;
    case 1:
        pendingAction_ = ActionType::OpenSettings;
        break;
    case 2:
        pendingAction_ = ActionType::ExitGame;
        break;
    default:
        break;
    }
}

void hudInitialMenu::update(int screenWidth, int screenHeight) {
    updateLayout(screenWidth, screenHeight);
    if (!enabled_) {
        return;
    }

    float mouseX = 0.0f;
    float mouseY = 0.0f;
    Input::getMousePosFramebuffer(mouseX, mouseY, screenWidth, screenHeight);

    hoveredButton_ = -1;
    for (int index = 0; index < kButtonCount; ++index) {
        if (pointInRect(mouseX, mouseY, layout_.buttonRects[static_cast<std::size_t>(index)])) {
            hoveredButton_ = index;
            selectedButton_ = index;
            break;
        }
    }

    auto& inputActions = InputMapping::InputActionSystem::instance();
    if (inputActions.wasPressed(InputActionIds::kUiUp)) {
        selectedButton_ = (selectedButton_ + kButtonCount - 1) % kButtonCount;
    }
    if (inputActions.wasPressed(InputActionIds::kUiDown)) {
        selectedButton_ = (selectedButton_ + 1) % kButtonCount;
    }
    if (inputActions.wasPressed(InputActionIds::kUiAccept)) {
        activateButton(selectedButton_);
        return;
    }
    if (inputActions.wasPressed(InputActionIds::kUiCancel)) {
        pendingAction_ = ActionType::ExitGame;
        return;
    }

    if (hoveredButton_ >= 0 && Input::mousePressed(GLFW_MOUSE_BUTTON_LEFT)) {
        activateButton(hoveredButton_);
    }
}

void hudInitialMenu::drawRect(Shader& shader, const glm::ivec4& rect,
                              const glm::vec4& color) const {
    if (rect.z <= 0 || rect.w <= 0) {
        return;
    }

    glm::mat4 model(1.0f);
    model = glm::translate(model, glm::vec3(rect.x, rect.y, 0.0f));
    model = glm::scale(model, glm::vec3(rect.z, rect.w, 1.0f));

    shader.setMat4("model", model);
    shader.setInt("isText", 0);
    shader.setVec4("color", color);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, HUD::getWhiteTexture());

    HUD::bindQuad();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    HUD::unbindQuad();
}

void hudInitialMenu::drawCenteredText(hudText& text, const std::string& value,
                                      const glm::ivec4& rect, int screenWidth,
                                      int screenHeight, Shader& shader) const {
    text.setText(value);
    const glm::vec2 size = text.measure();
    const int x = rect.x + static_cast<int>(std::round((rect.z - size.x) * 0.5f));
    const int y = rect.y + static_cast<int>(std::round((rect.w - size.y) * 0.5f));
    text.setPosition(x, y);
    text.draw(shader, screenWidth, screenHeight);
}

void hudInitialMenu::draw(Shader& shader, int screenWidth, int screenHeight) {
    updateLayout(screenWidth, screenHeight);

    drawRect(shader, expandRect(layout_.panelRect, 4),
             glm::vec4(0.18f, 0.21f, 0.32f, 0.0f));
    drawRect(shader, layout_.panelRect, glm::vec4(0.07f, 0.08f, 0.11f, 0.0f));

    drawCenteredText(*titleText_, "Voxel Paradox", layout_.titleRect,
                     screenWidth, screenHeight, shader);

    const glm::ivec4 subtitleRect(
        layout_.titleRect.x,
        layout_.titleRect.y + 64,
        layout_.titleRect.z,
        26
    );
    drawCenteredText(*subtitleText_, "Your universe is nothing but a dream.", subtitleRect,
                     screenWidth, screenHeight, shader);

    const char* labels[kButtonCount] = {"Start Game", "Settings", "Exit Game"};
    for (int index = 0; index < kButtonCount; ++index) {
        const glm::ivec4& rect = layout_.buttonRects[static_cast<std::size_t>(index)];
        const bool selected = enabled_ && index == selectedButton_;
        const bool hovered = enabled_ && index == hoveredButton_;
        const bool exitButton = index == 2;

        drawRect(shader, expandRect(rect, 2),
                 selected ? glm::vec4(0.38f, 0.44f, 0.66f, 1.0f)
                          : glm::vec4(0.18f, 0.21f, 0.32f, 1.0f));
        drawRect(shader, rect,
                 exitButton
                     ? (hovered ? glm::vec4(0.17f, 0.11f, 0.13f, 1.0f)
                                : glm::vec4(0.12f, 0.09f, 0.11f, 1.0f))
                     : (hovered ? glm::vec4(0.15f, 0.17f, 0.25f, 1.0f)
                                : glm::vec4(0.12f, 0.14f, 0.21f, 1.0f)));

        hudText* buttonText = buttonTexts_[static_cast<std::size_t>(index)];
        if (!buttonText) {
            continue;
        }
        buttonText->setColor(exitButton ? glm::vec3(0.98f, 0.90f, 0.90f)
                                        : glm::vec3(0.95f, 0.97f, 1.0f));
        drawCenteredText(*buttonText, labels[index], rect, screenWidth, screenHeight, shader);
    }
}
