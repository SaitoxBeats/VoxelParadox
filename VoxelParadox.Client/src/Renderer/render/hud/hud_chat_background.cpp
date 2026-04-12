// File: VoxelParadox.Client/src/Renderer/render/hud/hud_chat_background.cpp
// Purpose: implements the dedicated chat background inside the HUD subsystem.

#include "hud_chat_background.hpp"

// 1. Standard
#include <algorithm>

// 2. External
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

// 3. Project
#include "hud.hpp"
#include "runtime/state/game_chat.hpp"

namespace {

constexpr float kChatBackgroundLeftMargin = 12.0f;
constexpr float kChatBackgroundWidth = 560.0f;
constexpr float kChatInputRightMargin = 12.0f;
constexpr float kChatTopBackgroundBottomMargin = 52.0f;
constexpr float kChatTopBackgroundHeight = 160.0f;
constexpr float kChatInputBackgroundBottomMargin = 14.0f;
constexpr float kChatInputBackgroundHeight = 42.0f;
constexpr int kPanelBorderThickness = 2;

const glm::vec4 kHistoryBorderColor (0.0f, 0.0f, 0.0f, 0.0f);
const glm::vec4 kHistoryFillColor   (0.0f, 0.0f, 0.0f, 0.30f);
const glm::vec4 kInputBorderColor   (0.0f, 0.0f, 0.0f, 0.0f);
const glm::vec4 kInputFillColor     (0.0f, 0.0f, 0.0f, 0.30f);

glm::ivec4 makeBottomLeftRect(int screenHeight, float leftMargin, float bottomMargin,
                              float width, float height) {
    return glm::ivec4(static_cast<int>(leftMargin),
                      static_cast<int>(screenHeight - bottomMargin - height),
                      static_cast<int>(width),
                      static_cast<int>(height));
}

glm::ivec4 makeBottomStretchRect(int screenWidth, int screenHeight,
                                 float leftMargin, float rightMargin,
                                 float bottomMargin, float height) {
    const int x = static_cast<int>(leftMargin);
    const int y = static_cast<int>(screenHeight - bottomMargin - height);
    const int width = std::max(0, screenWidth - x - static_cast<int>(rightMargin));

    return glm::ivec4(
        x,
        y,
        width,
        static_cast<int>(height)
    );
}

glm::ivec4 insetRect(const glm::ivec4& rect, int amount) {
    return glm::ivec4(rect.x + amount, rect.y + amount,
                      std::max(0, rect.z - amount * 2),
                      std::max(0, rect.w - amount * 2));
}

} // namespace

hudChatBackground::hudChatBackground(const GameChat* chat) : chat(chat) {}

void hudChatBackground::drawRect(Shader& shader, const glm::ivec4& rect,
                                 const glm::vec4& color) const {
    if (rect.z <= 0 || rect.w <= 0) {
        return;
    }

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(static_cast<float>(rect.x),
                                            static_cast<float>(rect.y), 0.0f));
    model = glm::scale(model, glm::vec3(static_cast<float>(rect.z),
                                        static_cast<float>(rect.w), 1.0f));

    shader.setMat4("model", model);
    shader.setInt("isText", 0);
    shader.setVec4("color", color);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, HUD::getWhiteTexture());
    HUD::bindQuad();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    HUD::unbindQuad();
}

void hudChatBackground::drawPanel(Shader& shader, const glm::ivec4& rect,
                                  const glm::vec4& borderColor,
                                  const glm::vec4& fillColor) const {
    drawRect(shader, rect, borderColor);
    drawRect(shader, insetRect(rect, kPanelBorderThickness), fillColor);
}

bool hudChatBackground::shouldDrawTopBackground() const {
    if (!chat) {
        return false;
    }

    return chat->visibleHistoryLineCount() > 0 || chat->visibleSuggestionLineCount() > 0;
}

void hudChatBackground::draw(Shader& shader, int screenWidth, int screenHeight) {
    if (!chat) {
        return;
    }

    if (shouldDrawTopBackground()) {
        drawPanel(shader,
                  makeBottomLeftRect(screenHeight, kChatBackgroundLeftMargin,
                                     kChatTopBackgroundBottomMargin,
                                     kChatBackgroundWidth,
                                     kChatTopBackgroundHeight),
                  kHistoryBorderColor,
                  kHistoryFillColor);
    }

    if (chat->isOpen()) {
        const glm::ivec4 inputRect = makeBottomStretchRect(screenWidth, screenHeight,
                                                           kChatBackgroundLeftMargin,
                                                           kChatInputRightMargin,
                                                           kChatInputBackgroundBottomMargin,
                                                           kChatInputBackgroundHeight);
        drawPanel(shader,
                  inputRect,
                  kInputBorderColor,
                  kInputFillColor);

        glm::ivec4 selectionRect(0);
        if (chat->tryGetInputSelectionRect(selectionRect)) {
            drawRect(shader, selectionRect, glm::vec4(0.3f, 0.45f, 0.9f, 0.65f));
        }

        glm::ivec4 caretRect(0);
        if (chat->tryGetInputCaretRect(caretRect)) {
            drawRect(shader, caretRect, glm::vec4(1.0f, 0.95f, 0.6f, 0.95f));
        }
    }
}
