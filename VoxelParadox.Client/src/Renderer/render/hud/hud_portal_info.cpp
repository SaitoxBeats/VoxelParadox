// File: VoxelParadox.Client/src/Renderer/render/hud/hud_portal_info.cpp
// Role: Implements "hud portal info" within the "render hud" subsystem.
// Flow: Concentrates types, data, and routines used by this runtime point in a documented and consistent manner.

#pragma region Includes

// 1. Standard Library
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

// 2. Third-party Libraries
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

// 3. Engine & Core Modules
#include "engine/engine.hpp"
#include "engine/input.hpp"
#include "world/persistence/world_stack.hpp"

// 4. Local Project Headers (Player/HUD)
#include "player/player.hpp"
#include "hud.hpp"
#include "hud_text.hpp"
#include "hud_portal_info.hpp"

#pragma endregion

#pragma region HudPortalInfoImplementation

namespace {

    // --- Layout Constants & Configuration ---
    constexpr int kScreenPadding = 4;
    constexpr int kAnchorOffset = 12;
    constexpr int kInputGap = 8;
    constexpr int kInputPadding = 8;
    constexpr std::size_t kMaxUniverseNameLength = 24;
    constexpr double kKeyRepeatDelay = 0.35;
    constexpr double kKeyRepeatInterval = 0.045;
    constexpr float kCaretWidth = 2.0f;

    // Function: Executes 'trimSpaces' for the portal info panel.
    // Detail: Uses 'value' to encapsulate this specific subsystem step.
    std::string trimSpaces(const std::string& value) {
        const std::size_t start = value.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return {};

        const std::size_t end = value.find_last_not_of(" \t\r\n");
        return value.substr(start, end - start + 1);
    }

    // Function: Executes 'formatSeed' for the portal info panel.
    std::string formatSeed(std::uint32_t seed) {
        std::ostringstream out;
        out << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << seed;
        return out.str();
    }

    // Function: Executes 'displayNameOrFallback' for the portal info panel.
    std::string displayNameOrFallback(const std::string& universeName) {
        return universeName.empty() ? "Unnamed" : universeName;
    }

    // Function: Assembles 'makePortalInfoLabel' for the portal info panel.
    std::string makePortalInfoLabel(const std::string& universeName,
        std::uint32_t childSeed,
        const BiomeSelection& childBiome) {
        return "Name: " + displayNameOrFallback(universeName) + "\nSeed: " +
            formatSeed(childSeed) + "\nBiome: " + childBiome.displayName +
            "\nPress \"Y\" to rename this universe";
    }

    // Function: Assembles 'makeInputLabel' for the portal info panel.
    std::string makeInputLabel(const std::string& currentName) {
        return currentName.empty() ? "Type a universe name" : currentName;
    }

    bool pointInRect(float x, float y, const glm::ivec2& pos, const glm::vec2& size) {
        return x >= static_cast<float>(pos.x) &&
            y >= static_cast<float>(pos.y) &&
            x <= static_cast<float>(pos.x + size.x) &&
            y <= static_cast<float>(pos.y + size.y);
    }

    // Function: Renders 'drawRect' for visual elements in the portal info panel.
    void drawRect(Shader& shader, const glm::ivec2& pos, const glm::vec2& size,
        const glm::vec4& color) {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3((float)pos.x, (float)pos.y, 0.0f));
        model = glm::scale(model, glm::vec3(size.x, size.y, 1.0f));

        shader.setMat4("model", model);
        shader.setInt("isText", 0);
        shader.setVec4("color", color);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

} // namespace

// --- Lifecycle Management ---

hudPortalInfo::hudPortalInfo(Player* player, WorldStack* worldStack, int fontSize,
    const std::string& fontPath)
    : player(player), worldStack(worldStack), fontSize(fontSize), fontPath(fontPath) {

    text = new hudText("", 0, 0, glm::vec2(1.0f), fontSize, fontPath);
    text->setColor(glm::vec3(1.0f));

    inputText = new hudText("", 0, 0, glm::vec2(1.0f), fontSize, fontPath);
    inputText->setColor(glm::vec3(1.0f, 0.95f, 0.6f));
}

hudPortalInfo::~hudPortalInfo() {
    delete text;
    text = nullptr;

    delete inputText;
    inputText = nullptr;
}

// --- State Queries ---

bool hudPortalInfo::isEditing() const { return editing; }

bool hudPortalInfo::cancelEditing() {
    if (!editing) return false;
    endEditing(false);
    return true;
}

bool hudPortalInfo::getPortalTarget(glm::ivec3& block, std::uint32_t& childSeed,
    BiomeSelection& childBiome) const {
    if (!worldStack) return false;

    FractalWorld* world = worldStack->currentWorld();
    if (!world) return false;
    if (world->getBlock(block) != BlockIds::PORTAL) return false;

    auto it = world->portalBlocks.find(block);
    if (it == world->portalBlocks.end()) return false;

    childSeed = it->second;
    childBiome = worldStack->getResolvedPortalBiomeSelection(*world, block, childSeed);
    return true;
}

// --- Editing Logic ---

void hudPortalInfo::beginEditing(const glm::ivec3& block, std::uint32_t childSeed,
    const BiomeSelection& childBiome) {
    editing = true;
    editingBlock = block;
    editingSeed = childSeed;
    editingBiome = childBiome;

    editingInput.setText(worldStack ? worldStack->getUniverseName(childSeed, childBiome)
        : std::string());

    editingInput.resetRepeats();
    drawCaret = true;
    drawSelection = false;
    showPlaceholder = editingInput.empty();

    Input::enableTextInput(true);
    Input::setCursorVisible(true);
}

void hudPortalInfo::endEditing(bool commit) {
    if (commit && worldStack) {
        worldStack->setUniverseName(editingSeed, editingBiome,
            trimSpaces(editingInput.text));
    }

    editing = false;
    editingInput.setText({});
    editingInput.resetRepeats();
    drawCaret = false;
    drawSelection = false;
    showPlaceholder = false;

    Input::enableTextInput(false);
    Input::setCursorVisible(false);
}

// --- Input Handling ---

bool hudPortalInfo::hasSelection() const {
    return editingInput.hasSelection();
}

void hudPortalInfo::clearSelection() {
    editingInput.clearSelection();
}

void hudPortalInfo::selectAll() {
    editingInput.selectAll();
}

void hudPortalInfo::deleteSelection() {
    editingInput.deleteSelection();
}

void hudPortalInfo::insertText(const std::string& typed) {
    editingInput.insertText(typed, kMaxUniverseNameLength);
}

void hudPortalInfo::moveCaretLeft() {
    editingInput.moveCaretLeft();
}

void hudPortalInfo::moveCaretRight() {
    editingInput.moveCaretRight();
}

void hudPortalInfo::eraseBackward() {
    editingInput.eraseBackward();
}

void hudPortalInfo::eraseForward() {
    editingInput.eraseForward();
}

void hudPortalInfo::placeCaretFromMouse(float mouseX) {
    const float localX =
        std::max(0.0f, mouseX - static_cast<float>(inputPanelPos.x + kInputPadding));

    editingInput.placeCaretFromMouse(localX, [this](std::size_t index) {
        return inputText->measureText(editingInput.text.substr(0, index)).x;
    });
}

void hudPortalInfo::updateMouseSelection(float mouseX) {
    const float localX =
        std::max(0.0f, mouseX - static_cast<float>(inputPanelPos.x + kInputPadding));

    editingInput.updateMouseSelection(localX, [this](std::size_t index) {
        return inputText->measureText(editingInput.text.substr(0, index)).x;
    });
}

bool hudPortalInfo::consumeHeldKey(int key, double now, double& nextRepeatTime) {
    return TextInputState::consumeHeldKey(key, now, nextRepeatTime,
        kKeyRepeatDelay, kKeyRepeatInterval);
}

// --- Core Runtime Flow ---

void hudPortalInfo::update(int screenWidth, int screenHeight) {
    // --- 1. Reset Frame State ---
    const glm::ivec2 previousInputPanelPos = inputPanelPos;
    const glm::vec2 previousInputPanelSize = inputPanelSize;
    visible = false;
    inputPanelSize = glm::vec2(0.0f);
    drawCaret = false;
    drawSelection = false;
    caretPixelOffset = 0.0f;
    selectionPixelStart = 0.0f;
    selectionPixelEnd = 0.0f;

    if (!player || !worldStack || !text || !inputText) return;

    glm::ivec3 block(0);
    std::uint32_t childSeed = 0;
    BiomeSelection childBiome{};

    // --- 2. Interaction Logic ---
    if (editing) {
        block = editingBlock;
        if (!getPortalTarget(block, childSeed, childBiome)) {
            endEditing(false);
            return;
        }
        editingSeed = childSeed;
        editingBiome = childBiome;
    }
    else {
        if (!player->hasTargetBlock()) return;
        block = player->getTargetBlock();
        if (!getPortalTarget(block, childSeed, childBiome)) return;

        if (Input::keyPressed(GLFW_KEY_Y)) {
            beginEditing(block, childSeed, childBiome);
        }
    }

    // --- 3. Text Input Processing ---
    if (editing) {
        const double now = ENGINE::GETTIME();
        float mouseX = 0.0f;
        float mouseY = 0.0f;
        Input::getMousePosFramebuffer(mouseX, mouseY, screenWidth, screenHeight);

        if (Input::mousePressed(GLFW_MOUSE_BUTTON_LEFT) &&
            pointInRect(mouseX, mouseY, previousInputPanelPos, previousInputPanelSize)) {
            editingInput.beginMouseSelection(
                std::max(0.0f, mouseX -
                                   static_cast<float>(previousInputPanelPos.x + kInputPadding)),
                [this](std::size_t index) {
                    return inputText->measureText(editingInput.text.substr(0, index)).x;
                }
            );
        }

        if (editingInput.mouseSelecting) {
            if (Input::mouseDown(GLFW_MOUSE_BUTTON_LEFT)) {
                inputPanelPos = previousInputPanelPos;
                inputPanelSize = previousInputPanelSize;
                updateMouseSelection(mouseX);
            }
            else {
                editingInput.endMouseSelection();
            }
        }

        editingInput.handleKeyboardEditing(now, kMaxUniverseNameLength);

        if (Input::keyPressed(GLFW_KEY_ENTER) || Input::keyPressed(GLFW_KEY_KP_ENTER)) {
            endEditing(true);
        }
        else if (Input::keyPressed(GLFW_KEY_ESCAPE)) {
            endEditing(false);
        }
    }

    // --- 4. HUD Coordinate & Layout Calculation ---
    const std::string storedName = worldStack->getUniverseName(childSeed, childBiome);
    const std::string previewName = editing ? trimSpaces(editingInput.text) : storedName;
    text->setText(makePortalInfoLabel(previewName, childSeed, childBiome));

    const glm::vec3 worldPos = glm::vec3(block) + glm::vec3(0.5f);
    glm::vec2 screenPos(0.0f);
    float ndcDepth = 0.0f;

    if (!player->camera.WorldToScreenPoint(worldPos, screenWidth, screenHeight, screenPos, &ndcDepth))
        return;
    if (ndcDepth < -1.0f || ndcDepth > 1.0f) return;

    const glm::vec2 infoSize = text->measure();
    glm::vec2 maxSize = infoSize;
    glm::vec2 inputInnerSize(0.0f);

    if (editing) {
        inputText->setText(makeInputLabel(editingInput.text));
        showPlaceholder = editingInput.empty();
        inputText->setColor(showPlaceholder ? glm::vec3(0.7f, 0.7f, 0.7f)
            : glm::vec3(1.0f, 0.95f, 0.6f));
        inputInnerSize = inputText->measure();
        inputPanelSize = inputInnerSize + glm::vec2((float)kInputPadding * 2.0f);
        maxSize.x = std::max(maxSize.x, inputPanelSize.x);
    }

    int x = (int)std::round(screenPos.x) + kAnchorOffset;
    int y = (int)std::round(screenPos.y) + kAnchorOffset;

    const int maxX = std::max(kScreenPadding, screenWidth - (int)std::ceil(maxSize.x) - kScreenPadding);
    x = std::clamp(x, kScreenPadding, maxX);

    int minY = kScreenPadding;
    if (editing) {
        minY += (int)std::ceil(inputPanelSize.y) + kInputGap;
    }
    const int maxY = std::max(minY, screenHeight - (int)std::ceil(infoSize.y) - kScreenPadding);
    y = std::clamp(y, minY, maxY);

    text->setPosition(x, y);

    // --- 5. Caret & Selection Visuals ---
    if (editing) {
        inputPanelPos = glm::ivec2(x, y - kInputGap - (int)std::ceil(inputPanelSize.y));
        inputText->setPosition(inputPanelPos.x + kInputPadding, inputPanelPos.y + kInputPadding);

        if (!showPlaceholder) {
            editingInput.caretIndex = std::min(editingInput.caretIndex, editingInput.text.size());
            const std::string caretPrefix = editingInput.text.substr(0, editingInput.caretIndex);
            caretPixelOffset = inputText->measureText(caretPrefix).x;
            drawCaret = std::fmod(ENGINE::GETTIME(), 1.0) < 0.5;

            if (hasSelection()) {
                const std::size_t first = std::min(editingInput.selectionStart, editingInput.selectionEnd);
                const std::size_t last = std::max(editingInput.selectionStart, editingInput.selectionEnd);

                selectionPixelStart = inputText->measureText(editingInput.text.substr(0, first)).x;
                selectionPixelEnd = inputText->measureText(editingInput.text.substr(0, last)).x;
                drawSelection = selectionPixelEnd > selectionPixelStart;
            }
        }
        else {
            drawCaret = std::fmod(ENGINE::GETTIME(), 1.0) < 0.5;
        }
    }

    visible = true;
}

void hudPortalInfo::draw(Shader& shader, int screenWidth, int screenHeight) {
    if (!visible || !text) return;

    if (editing && inputText) {
        // --- 1. Draw Input Field Background & Selection ---
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, HUD::getWhiteTexture());
        HUD::bindQuad();

        // Border and Background
        drawRect(shader, inputPanelPos - glm::ivec2(1), inputPanelSize + glm::vec2(2.0f),
            glm::vec4(0.95f, 0.85f, 0.3f, 0.95f));
        drawRect(shader, inputPanelPos, inputPanelSize, glm::vec4(0.05f, 0.05f, 0.05f, 0.92f));

        // Text Selection
        if (drawSelection) {
            drawRect(shader,
                glm::ivec2(inputText->getX() + (int)std::round(selectionPixelStart),
                    inputText->getY()),
                glm::vec2(selectionPixelEnd - selectionPixelStart, inputText->measure().y),
                glm::vec4(0.3f, 0.45f, 0.9f, 0.65f));
        }

        HUD::unbindQuad();
        glBindTexture(GL_TEXTURE_2D, 0);

        // --- 2. Draw Input Text ---
        inputText->draw(shader, screenWidth, screenHeight);

        // --- 3. Draw Caret ---
        if (drawCaret) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, HUD::getWhiteTexture());
            HUD::bindQuad();
            drawRect(shader,
                glm::ivec2(inputText->getX() + (int)std::round(caretPixelOffset),
                    inputText->getY()),
                glm::vec2(kCaretWidth, inputText->measure().y),
                glm::vec4(1.0f, 0.95f, 0.6f, 0.95f));
            HUD::unbindQuad();
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }

    // --- 4. Draw Main Info Text ---
    text->draw(shader, screenWidth, screenHeight);
}

#pragma endregion
