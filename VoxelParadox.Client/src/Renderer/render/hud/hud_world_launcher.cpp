// File: VoxelParadox.Client/src/Renderer/render/hud/hud_world_launcher.cpp
// Role: Implements the World Launcher (Main Menu) HUD for world selection and creation.
// Flow: Handles world list browsing, text input for new worlds, and loading states.

// 1. Standard Library
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

// 2. Third-party Libraries
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

// 3. Local Project Modules
#include "hud_world_launcher.hpp"
#include "engine/engine.hpp"
#include "engine/input.hpp"
#include "hud.hpp"
#include "hud_text.hpp"
#include "input/input_action_ids.hpp"
#include "input/input_action_system.hpp"

namespace {

    // --- 1. Layout Constants & Configuration ---

    constexpr float kPanelWidthRatio = 0.42f;
    constexpr float kPanelHeightRatio = 0.82f;
    constexpr float kPanelMinWidth = 560.0f;
    constexpr float kPanelMaxWidth = 760.0f;
    constexpr float kPanelMinHeight = 560.0f;
    constexpr float kPanelMaxHeight = 860.0f;

    constexpr int kOuterPadding = 18;
    constexpr int kListInnerPadding = 10;
    constexpr int kListScrollbarWidth = 16;
    constexpr int kListScrollbarMargin = 6;

    constexpr float kRowHeight = 84.0f;
    constexpr float kRowGap = 8.0f;
    constexpr float kInputHeight = 36.0f;
    constexpr float kButtonHeight = 52.0f;
    constexpr float kSectionGap = 18.0f;
    constexpr float kFooterGap = 14.0f;
    constexpr int kButtonGap = 12;
    constexpr float kUtilityButtonWidth = 124.0f;
    constexpr int kModalPadding = 18;
    constexpr int kModalButtonGap = 12;
    constexpr float kModalMinWidth = 420.0f;
    constexpr float kModalMaxWidth = 560.0f;
    constexpr float kModalInputHeight = 38.0f;
    constexpr float kModalButtonHeight = 46.0f;

    constexpr double kDoubleClickWindowSeconds = 0.30;
    constexpr int kInputPadding = 10;
    constexpr double kKeyRepeatDelay = 0.35;
    constexpr double kKeyRepeatInterval = 0.045;
    constexpr float kCaretWidth = 2.0f;

    // --- 2. Small Utility Helpers ---

    std::string formatPlaytime(double seconds) {
        const int totalSeconds = static_cast<int>(seconds < 0.0 ? 0.0 : std::round(seconds));
        const int hours = totalSeconds / 3600;
        const int minutes = (totalSeconds % 3600) / 60;
        const int remainingSeconds = totalSeconds % 60;

        char buffer[32];
        if (hours > 0) {
            std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", hours, minutes, remainingSeconds);
        }
        else {
            std::snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes, remainingSeconds);
        }
        return buffer;
    }

    std::string formatSeed(std::uint32_t seed) {
        return std::to_string(seed);
    }

    glm::ivec4 expandRect(const glm::ivec4& rect, int amount) {
        return glm::ivec4(
            rect.x - amount,
            rect.y - amount,
            rect.z + amount * 2,
            rect.w + amount * 2
        );
    }

    float clampFloat(float value, float minValue, float maxValue) {
        return std::max(minValue, std::min(value, maxValue));
    }

}  // namespace

// --- 3. Construction And Lifecycle ---

hudWorldLauncher::hudWorldLauncher(const std::string& fontPath) {
    // Initialize UI Text elements
    titleText_ = new hudText("Worlds", 0, 0, glm::vec2(1.0f), 24, fontPath);
    subtitleText_ = new hudText("Choose a world or create a new one.", 0, 0, glm::vec2(1.0f), 16, fontPath);
    rowTitleText_ = new hudText("", 0, 0, glm::vec2(1.0f), 16, fontPath);
    rowDetailText_ = new hudText("", 0, 0, glm::vec2(1.0f), 12, fontPath);
    emptyText_ = new hudText("No worlds created yet.", 0, 0, glm::vec2(1.0f), 16, fontPath);
    inputText_ = new hudText("", 0, 0, glm::vec2(1.0f), 18, fontPath);
    actionButtonText_ = new hudText("Generate", 0, 0, glm::vec2(1.0f), 18, fontPath);
    renameButtonText_ = new hudText("Rename", 0, 0, glm::vec2(1.0f), 18, fontPath);
    deleteButtonText_ = new hudText("Delete", 0, 0, glm::vec2(1.0f), 18, fontPath);
    exitButtonText_ = new hudText("Exit Game", 0, 0, glm::vec2(1.0f), 18, fontPath);
    statusText_ = new hudText("", 0, 0, glm::vec2(1.0f), 14, fontPath);
    loadingText_ = new hudText("Loading World", 0, 0, glm::vec2(1.0f), 26, fontPath);
    loadingDotsText_ = new hudText("", 0, 0, glm::vec2(1.0f), 20, fontPath);
    modalTitleText_ = new hudText("", 0, 0, glm::vec2(1.0f), 22, fontPath);
    modalBodyText_ = new hudText("", 0, 0, glm::vec2(1.0f), 14, fontPath);
    modalInputText_ = new hudText("", 0, 0, glm::vec2(1.0f), 18, fontPath);
    modalConfirmButtonText_ = new hudText("", 0, 0, glm::vec2(1.0f), 18, fontPath);
    modalCancelButtonText_ = new hudText("Cancel", 0, 0, glm::vec2(1.0f), 18, fontPath);

    // Apply color theme
    titleText_->setColor(glm::vec3(0.97f, 0.98f, 1.0f));
    subtitleText_->setColor(glm::vec3(0.68f, 0.72f, 0.80f));
    rowTitleText_->setColor(glm::vec3(0.97f, 0.98f, 1.0f));
    rowDetailText_->setColor(glm::vec3(0.68f, 0.72f, 0.80f));
    emptyText_->setColor(glm::vec3(0.68f, 0.72f, 0.80f));
    inputText_->setColor(glm::vec3(0.95f, 0.97f, 1.0f));
    actionButtonText_->setColor(glm::vec3(0.95f, 0.97f, 1.0f));
    renameButtonText_->setColor(glm::vec3(0.92f, 0.94f, 1.0f));
    deleteButtonText_->setColor(glm::vec3(0.98f, 0.90f, 0.90f));
    exitButtonText_->setColor(glm::vec3(0.95f, 0.82f, 0.82f));
    statusText_->setColor(glm::vec3(1.0f, 0.72f, 0.72f));
    loadingText_->setColor(glm::vec3(0.97f, 0.98f, 1.0f));
    loadingDotsText_->setColor(glm::vec3(0.97f, 0.98f, 1.0f));
    modalTitleText_->setColor(glm::vec3(0.97f, 0.98f, 1.0f));
    modalBodyText_->setColor(glm::vec3(0.68f, 0.72f, 0.80f));
    modalInputText_->setColor(glm::vec3(0.95f, 0.97f, 1.0f));
    modalConfirmButtonText_->setColor(glm::vec3(0.95f, 0.97f, 1.0f));
    modalCancelButtonText_->setColor(glm::vec3(0.95f, 0.97f, 1.0f));
}

hudWorldLauncher::~hudWorldLauncher() {
    delete titleText_;
    delete subtitleText_;
    delete rowTitleText_;
    delete rowDetailText_;
    delete emptyText_;
    delete inputText_;
    delete actionButtonText_;
    delete renameButtonText_;
    delete deleteButtonText_;
    delete exitButtonText_;
    delete statusText_;
    delete loadingText_;
    delete loadingDotsText_;
    delete modalTitleText_;
    delete modalBodyText_;
    delete modalInputText_;
    delete modalConfirmButtonText_;
    delete modalCancelButtonText_;
}

// --- 4. State Management ---

void hudWorldLauncher::setWorlds(const std::vector<WorldSaveService::WorldSummary>& worlds) {
    const std::filesystem::path previousSelection =
        selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(worlds_.size())
        ? worlds_[static_cast<std::size_t>(selectedIndex_)].paths.worldDirectory
        : std::filesystem::path{};

    worlds_ = worlds;
    selectedIndex_ = -1;

    if (!previousSelection.empty()) {
        setSelectedWorldDirectory(previousSelection);
    }

    clampSelection();
}

void hudWorldLauncher::setLoading(bool loading) {
    loading_ = loading;
}

void hudWorldLauncher::setStatusMessage(const std::string& statusMessage) {
    statusMessage_ = statusMessage;
}

void hudWorldLauncher::setSelectedWorldDirectory(const std::filesystem::path& worldDirectory) {
    selectedIndex_ = -1;

    for (int index = 0; index < static_cast<int>(worlds_.size()); ++index) {
        if (worlds_[static_cast<std::size_t>(index)].paths.worldDirectory == worldDirectory) {
            selectedIndex_ = index;
            textFieldFocused_ = false;
            break;
        }
    }

    clampSelection();
}

hudWorldLauncher::ActionRequest hudWorldLauncher::consumeRequest() {
    ActionRequest request = pendingRequest_;
    pendingRequest_ = {};
    return request;
}

// --- 5. Layout And Hit Testing ---

void hudWorldLauncher::updateLayout(int screenWidth, int screenHeight) {
    // Panel calculations
    const float panelWidth = clampFloat(screenWidth * kPanelWidthRatio, kPanelMinWidth, kPanelMaxWidth);
    const float panelHeight = clampFloat(screenHeight * kPanelHeightRatio, kPanelMinHeight, kPanelMaxHeight);

    const int panelX = static_cast<int>(std::round((screenWidth - panelWidth) * 0.5f));
    const int panelY = static_cast<int>(std::round((screenHeight - panelHeight) * 0.5f));
    const int contentWidth = static_cast<int>(std::round(panelWidth)) - kOuterPadding * 2;

    // Header calculations
    const int titleHeight = static_cast<int>(std::round(titleText_->measure().y));
    const int subtitleHeight = static_cast<int>(std::round(subtitleText_->measure().y));
    const int headerHeight = titleHeight + subtitleHeight + 18;

    // Component placement
    const int listTop = panelY + kOuterPadding + headerHeight;
    const int buttonStackHeight = static_cast<int>(std::round(
        kInputHeight + kSectionGap + kButtonHeight + kFooterGap + kButtonHeight
    ));
    const int listHeight = std::max(240, static_cast<int>(std::round(panelHeight)) - headerHeight - buttonStackHeight - kOuterPadding * 2 - 20);

    layout_.listRect = glm::ivec4(
        panelX + kOuterPadding,
        listTop,
        contentWidth,
        listHeight
    );

    layout_.inputRect = glm::ivec4(
        panelX + kOuterPadding,
        listTop + listHeight + static_cast<int>(kSectionGap),
        contentWidth,
        static_cast<int>(std::round(kInputHeight))
    );

    layout_.actionButtonRect = glm::ivec4(
        panelX + kOuterPadding,
        layout_.inputRect.y + layout_.inputRect.w + static_cast<int>(std::round(kSectionGap)),
        contentWidth - static_cast<int>(std::round(kUtilityButtonWidth * 2.0f)) - kButtonGap * 2,
        static_cast<int>(std::round(kButtonHeight))
    );

    layout_.renameButtonRect = glm::ivec4(
        layout_.actionButtonRect.x + layout_.actionButtonRect.z + kButtonGap,
        layout_.actionButtonRect.y,
        static_cast<int>(std::round(kUtilityButtonWidth)),
        static_cast<int>(std::round(kButtonHeight))
    );

    layout_.deleteButtonRect = glm::ivec4(
        layout_.renameButtonRect.x + layout_.renameButtonRect.z + kButtonGap,
        layout_.actionButtonRect.y,
        static_cast<int>(std::round(kUtilityButtonWidth)),
        static_cast<int>(std::round(kButtonHeight))
    );

    layout_.exitButtonRect = glm::ivec4(
        panelX + kOuterPadding,
        layout_.actionButtonRect.y + layout_.actionButtonRect.w + static_cast<int>(std::round(kFooterGap)),
        contentWidth,
        static_cast<int>(std::round(kButtonHeight))
    );

    layout_.rowHeight = kRowHeight;
    layout_.rowGap = kRowGap;
    layout_.visibleRows = std::max(1, static_cast<int>(std::floor(
        (layout_.listRect.w - kListInnerPadding * 2 + layout_.rowGap) / (layout_.rowHeight + layout_.rowGap)
    )));
}

void hudWorldLauncher::updateModalLayout(int screenWidth, int screenHeight) {
    const bool renameModal = modalType_ == ModalType::RenameWorld;
    const float panelWidth = clampFloat(
        screenWidth * 0.38f, kModalMinWidth, kModalMaxWidth
    );
    const float panelHeight = renameModal ? 236.0f : 194.0f;

    const int panelX = static_cast<int>(std::round((screenWidth - panelWidth) * 0.5f));
    const int panelY = static_cast<int>(std::round((screenHeight - panelHeight) * 0.5f));

    modalLayout_.panelRect = glm::ivec4(
        panelX,
        panelY,
        static_cast<int>(std::round(panelWidth)),
        static_cast<int>(std::round(panelHeight))
    );

    modalLayout_.titleRect = glm::ivec4(
        panelX + kModalPadding,
        panelY + kModalPadding,
        modalLayout_.panelRect.z - kModalPadding * 2,
        28
    );

    modalLayout_.bodyRect = glm::ivec4(
        panelX + kModalPadding,
        modalLayout_.titleRect.y + modalLayout_.titleRect.w + 10,
        modalLayout_.panelRect.z - kModalPadding * 2,
        22
    );

    modalLayout_.inputRect = glm::ivec4(
        panelX + kModalPadding,
        modalLayout_.bodyRect.y + modalLayout_.bodyRect.w + 16,
        modalLayout_.panelRect.z - kModalPadding * 2,
        static_cast<int>(std::round(kModalInputHeight))
    );

    const int buttonsY = renameModal
        ? modalLayout_.inputRect.y + modalLayout_.inputRect.w + 18
        : modalLayout_.bodyRect.y + modalLayout_.bodyRect.w + 22;
    const int buttonWidth =
        (modalLayout_.panelRect.z - kModalPadding * 2 - kModalButtonGap) / 2;

    modalLayout_.confirmButtonRect = glm::ivec4(
        panelX + kModalPadding,
        buttonsY,
        buttonWidth,
        static_cast<int>(std::round(kModalButtonHeight))
    );

    modalLayout_.cancelButtonRect = glm::ivec4(
        modalLayout_.confirmButtonRect.x + modalLayout_.confirmButtonRect.z + kModalButtonGap,
        buttonsY,
        buttonWidth,
        static_cast<int>(std::round(kModalButtonHeight))
    );
}

bool hudWorldLauncher::pointInRect(float x, float y, const glm::ivec4& rect) const {
    return x >= static_cast<float>(rect.x) &&
        y >= static_cast<float>(rect.y) &&
        x <= static_cast<float>(rect.x + rect.z) &&
        y <= static_cast<float>(rect.y + rect.w);
}

int hudWorldLauncher::hoveredWorldIndex(float mouseX, float mouseY) const {
    if (!pointInRect(mouseX, mouseY, layout_.listRect)) {
        return -1;
    }

    const int visibleRows = layout_.visibleRows;
    const int startIndex = scrollOffset_;
    const int endIndex = std::min(startIndex + visibleRows, static_cast<int>(worlds_.size()));

    const int rowWidth = layout_.listRect.z - kListInnerPadding * 2 - kListScrollbarWidth - kListScrollbarMargin;
    const int firstRowY = layout_.listRect.y + kListInnerPadding;

    for (int index = startIndex; index < endIndex; ++index) {
        const int rowOffset = index - startIndex;
        const glm::ivec4 rowRect(
            layout_.listRect.x + kListInnerPadding,
            firstRowY + static_cast<int>(std::round(rowOffset * (layout_.rowHeight + layout_.rowGap))),
            rowWidth,
            static_cast<int>(std::round(layout_.rowHeight))
        );

        if (pointInRect(mouseX, mouseY, rowRect)) {
            return index;
        }
    }

    return -1;
}

void hudWorldLauncher::clampSelection() {
    if (worlds_.empty()) {
        selectedIndex_ = -1;
        scrollOffset_ = 0;
        return;
    }

    selectedIndex_ = std::clamp(selectedIndex_, -1, static_cast<int>(worlds_.size()) - 1);
    const int maxScroll = std::max(0, static_cast<int>(worlds_.size()) - layout_.visibleRows);

    scrollOffset_ = std::clamp(scrollOffset_, 0, maxScroll);

    if (selectedIndex_ < 0) {
        return;
    }
    if (selectedIndex_ < scrollOffset_) {
        scrollOffset_ = selectedIndex_;
    }
    if (selectedIndex_ >= scrollOffset_ + layout_.visibleRows) {
        scrollOffset_ = selectedIndex_ - layout_.visibleRows + 1;
    }
}

// --- 6. Text Editing ---

bool hudWorldLauncher::hasSelection() const { return worldNameInput_.hasSelection(); }
void hudWorldLauncher::clearSelection() { worldNameInput_.clearSelection(); }
void hudWorldLauncher::selectAll() { worldNameInput_.selectAll(); }
void hudWorldLauncher::deleteSelection() { worldNameInput_.deleteSelection(); }
void hudWorldLauncher::insertText(const std::string& typed) { worldNameInput_.insertText(typed, kMaxWorldNameLength); }
void hudWorldLauncher::moveCaretLeft() { worldNameInput_.moveCaretLeft(); }
void hudWorldLauncher::moveCaretRight() { worldNameInput_.moveCaretRight(); }
void hudWorldLauncher::eraseBackward() { worldNameInput_.eraseBackward(); }
void hudWorldLauncher::eraseForward() { worldNameInput_.eraseForward(); }

bool hudWorldLauncher::consumeHeldKey(int key, double now, double& nextRepeatTime) {
    return TextInputState::consumeHeldKey(
        key, now, nextRepeatTime,
        kKeyRepeatDelay, kKeyRepeatInterval
    );
}

void hudWorldLauncher::placeCaretFromMouse(float mouseX) {
    const float localX = std::max(0.0f, mouseX - static_cast<float>(layout_.inputRect.x + kInputPadding));

    worldNameInput_.placeCaretFromMouse(localX, [this](std::size_t index) {
        return inputText_->measureText(worldNameInput_.text.substr(0, index)).x;
        });
}

void hudWorldLauncher::updateTextInput(float mouseX) {
    if (!textFieldFocused_ || loading_) {
        Input::consumeTypedChars();
        if (!textFieldFocused_) {
            worldNameInput_.resetRepeats();
            worldNameInput_.endMouseSelection();
            drawCaret_ = false;
            drawSelection_ = false;
        }
        return;
    }

    const double now = ENGINE::GETTIME();

    if (worldNameInput_.mouseSelecting) {
        if (Input::mouseDown(GLFW_MOUSE_BUTTON_LEFT)) {
            const float localX = std::max(
                0.0f,
                mouseX - static_cast<float>(layout_.inputRect.x + kInputPadding)
            );

            worldNameInput_.updateMouseSelection(localX, [this](std::size_t index) {
                return inputText_->measureText(worldNameInput_.text.substr(0, index)).x;
            });
        }
        else {
            worldNameInput_.endMouseSelection();
        }
    }

    const std::string previousText = worldNameInput_.text;
    worldNameInput_.handleKeyboardEditing(now, kMaxWorldNameLength);

    if (worldNameInput_.text != previousText) {
        selectedIndex_ = -1;
    }
}

void hudWorldLauncher::placeModalCaretFromMouse(float mouseX) {
    const float localX = std::max(
        0.0f,
        mouseX - static_cast<float>(modalLayout_.inputRect.x + kInputPadding)
    );

    modalWorldNameInput_.placeCaretFromMouse(localX, [this](std::size_t index) {
        return modalInputText_->measureText(
            modalWorldNameInput_.text.substr(0, index)
        ).x;
    });
}

void hudWorldLauncher::updateModalTextInput(float mouseX) {
    modalDrawCaret_ = false;
    modalDrawSelection_ = false;

    if (modalType_ != ModalType::RenameWorld || loading_) {
        Input::consumeTypedChars();
        modalWorldNameInput_.resetRepeats();
        modalWorldNameInput_.endMouseSelection();
        return;
    }

    const double now = ENGINE::GETTIME();

    if (modalWorldNameInput_.mouseSelecting) {
        if (Input::mouseDown(GLFW_MOUSE_BUTTON_LEFT)) {
            const float localX = std::max(
                0.0f,
                mouseX - static_cast<float>(modalLayout_.inputRect.x + kInputPadding)
            );

            modalWorldNameInput_.updateMouseSelection(localX, [this](std::size_t index) {
                return modalInputText_->measureText(
                    modalWorldNameInput_.text.substr(0, index)
                ).x;
            });
        }
        else {
            modalWorldNameInput_.endMouseSelection();
        }
    }

    modalWorldNameInput_.handleKeyboardEditing(now, kMaxWorldNameLength);
}

// --- 7. World Actions And Selection ---

void hudWorldLauncher::requestCreateWorld() {
    pendingRequest_.type = ActionType::CreateWorld;
    pendingRequest_.worldName = worldNameInput_.text;
    pendingRequest_.worldDirectory.clear();
}

void hudWorldLauncher::requestLoadWorld(int index) {
    if (index < 0 || index >= static_cast<int>(worlds_.size())) {
        return;
    }

    pendingRequest_.type = ActionType::LoadWorld;
    pendingRequest_.worldDirectory = worlds_[static_cast<std::size_t>(index)].paths.worldDirectory;
    pendingRequest_.worldName.clear();
}

void hudWorldLauncher::requestRenameWorld(int index) {
    if (index < 0 || index >= static_cast<int>(worlds_.size())) {
        return;
    }

    pendingRequest_.type = ActionType::RenameWorld;
    pendingRequest_.worldDirectory =
        worlds_[static_cast<std::size_t>(index)].paths.worldDirectory;
    pendingRequest_.worldName = modalWorldNameInput_.text;
    closeModal();
}

void hudWorldLauncher::requestDeleteWorld(int index) {
    if (index < 0 || index >= static_cast<int>(worlds_.size())) {
        return;
    }

    pendingRequest_.type = ActionType::DeleteWorld;
    pendingRequest_.worldDirectory =
        worlds_[static_cast<std::size_t>(index)].paths.worldDirectory;
    pendingRequest_.worldName.clear();
    closeModal();
}

void hudWorldLauncher::beginDeleteConfirmation(int index) {
    if (index < 0 || index >= static_cast<int>(worlds_.size()) || loading_) {
        return;
    }

    modalType_ = ModalType::ConfirmDelete;
    modalWorldIndex_ = index;
    modalWorldNameInput_ = {};
    modalDrawCaret_ = false;
    modalDrawSelection_ = false;
    textFieldFocused_ = false;
}

void hudWorldLauncher::beginRenameWorld(int index) {
    if (index < 0 || index >= static_cast<int>(worlds_.size()) || loading_) {
        return;
    }

    modalType_ = ModalType::RenameWorld;
    modalWorldIndex_ = index;
    modalWorldNameInput_ = {};
    modalWorldNameInput_.setText(
        worlds_[static_cast<std::size_t>(index)].manifest.displayName
    );
    modalDrawCaret_ = true;
    modalDrawSelection_ = false;
    textFieldFocused_ = false;
}

void hudWorldLauncher::closeModal() {
    modalType_ = ModalType::None;
    modalWorldIndex_ = -1;
    modalWorldNameInput_ = {};
    modalCaretPixelOffset_ = 0.0f;
    modalSelectionPixelStart_ = 0.0f;
    modalSelectionPixelEnd_ = 0.0f;
    modalDrawCaret_ = false;
    modalDrawSelection_ = false;
}

bool hudWorldLauncher::hasModal() const {
    return modalType_ != ModalType::None;
}

void hudWorldLauncher::updateSelection(float mouseX, float mouseY) {
    auto& inputActions = InputMapping::InputActionSystem::instance();

    hoveredIndex_ = hoveredWorldIndex(mouseX, mouseY);

    if (loading_) {
        return;
    }

    // Scroll handling
    if (pointInRect(mouseX, mouseY, layout_.listRect)) {
        const float scroll = Input::getScroll();
        if (scroll > 0.1f) {
            scrollOffset_ = std::max(0, scrollOffset_ - 1);
        }
        else if (scroll < -0.1f) {
            scrollOffset_ = std::min(
                std::max(0, static_cast<int>(worlds_.size()) - layout_.visibleRows),
                scrollOffset_ + 1
            );
        }
    }

    // Arrow keys navigation
    if (!textFieldFocused_ && !worlds_.empty()) {
        if (inputActions.wasPressed(InputActionIds::kUiUp)) {
            selectedIndex_ = (selectedIndex_ < 0) ? 0 : std::max(0, selectedIndex_ - 1);
            textFieldFocused_ = false;
        }

        if (inputActions.wasPressed(InputActionIds::kUiDown)) {
            selectedIndex_ = (selectedIndex_ < 0) ? 0 : std::min(static_cast<int>(worlds_.size()) - 1, selectedIndex_ + 1);
            textFieldFocused_ = false;
        }
    }

    // Mouse click handling
    if (!Input::mousePressed(GLFW_MOUSE_BUTTON_LEFT)) {
        clampSelection();
        return;
    }

    if (hoveredIndex_ >= 0) {
        const double now = ENGINE::GETTIME();
        const bool doubleClicked =
            hoveredIndex_ == lastClickedIndex_ &&
            lastClickTime_ >= 0.0 &&
            (now - lastClickTime_) <= kDoubleClickWindowSeconds;

        selectedIndex_ = hoveredIndex_;
        textFieldFocused_ = false;

        if (doubleClicked) {
            requestLoadWorld(selectedIndex_);
        }

        lastClickedIndex_ = hoveredIndex_;
        lastClickTime_ = now;
    }

    clampSelection();
}

void hudWorldLauncher::updateButtons(float mouseX, float mouseY) {
    auto& inputActions = InputMapping::InputActionSystem::instance();

    if (loading_) {
        return;
    }

    if (Input::mousePressed(GLFW_MOUSE_BUTTON_LEFT)) {
        if (pointInRect(mouseX, mouseY, layout_.inputRect)) {
            textFieldFocused_ = true;
            selectedIndex_ = -1;
            worldNameInput_.beginMouseSelection(
                std::max(0.0f, mouseX - static_cast<float>(layout_.inputRect.x + kInputPadding)),
                [this](std::size_t index) {
                    return inputText_->measureText(worldNameInput_.text.substr(0, index)).x;
                }
            );
            return;
        }

        textFieldFocused_ = false;
        worldNameInput_.endMouseSelection();

        if (pointInRect(mouseX, mouseY, layout_.actionButtonRect)) {
            if (selectedIndex_ >= 0) {
                requestLoadWorld(selectedIndex_);
            }
            else {
                requestCreateWorld();
            }
            return;
        }

        if (pointInRect(mouseX, mouseY, layout_.renameButtonRect) &&
            selectedIndex_ >= 0) {
            beginRenameWorld(selectedIndex_);
            return;
        }

        if (pointInRect(mouseX, mouseY, layout_.deleteButtonRect) &&
            selectedIndex_ >= 0) {
            beginDeleteConfirmation(selectedIndex_);
            return;
        }

        if (pointInRect(mouseX, mouseY, layout_.exitButtonRect)) {
            pendingRequest_.type = ActionType::ExitGame;
            pendingRequest_.worldName.clear();
            pendingRequest_.worldDirectory.clear();
            return;
        }
    }

    if (inputActions.wasPressed(InputActionIds::kUiAccept)) {
        if (selectedIndex_ >= 0 && !textFieldFocused_) {
            requestLoadWorld(selectedIndex_);
        }
        else {
            requestCreateWorld();
        }
    }
}

void hudWorldLauncher::updateModalButtons(float mouseX, float mouseY) {
    auto& inputActions = InputMapping::InputActionSystem::instance();

    if (!hasModal() || loading_) {
        return;
    }

    const bool renameModal = modalType_ == ModalType::RenameWorld;
    const bool confirmPressed = inputActions.wasPressed(InputActionIds::kUiAccept);
    const bool cancelPressed = inputActions.wasPressed(InputActionIds::kUiCancel);

    if (cancelPressed) {
        closeModal();
        return;
    }

    if (Input::mousePressed(GLFW_MOUSE_BUTTON_LEFT)) {
        if (renameModal && pointInRect(mouseX, mouseY, modalLayout_.inputRect)) {
            modalWorldNameInput_.beginMouseSelection(
                std::max(
                    0.0f,
                    mouseX - static_cast<float>(modalLayout_.inputRect.x + kInputPadding)
                ),
                [this](std::size_t index) {
                    return modalInputText_->measureText(
                        modalWorldNameInput_.text.substr(0, index)
                    ).x;
                }
            );
            return;
        }
    }

    const bool confirmClicked =
        Input::mousePressed(GLFW_MOUSE_BUTTON_LEFT) &&
        pointInRect(mouseX, mouseY, modalLayout_.confirmButtonRect);
    const bool cancelClicked =
        Input::mousePressed(GLFW_MOUSE_BUTTON_LEFT) &&
        pointInRect(mouseX, mouseY, modalLayout_.cancelButtonRect);

    if (cancelClicked) {
        closeModal();
        return;
    }

    if (!confirmPressed && !confirmClicked) {
        return;
    }

    if (modalType_ == ModalType::ConfirmDelete) {
        requestDeleteWorld(modalWorldIndex_);
        return;
    }

    if (modalType_ == ModalType::RenameWorld) {
        requestRenameWorld(modalWorldIndex_);
    }
}

// --- 8. Per-Frame Update ---

void hudWorldLauncher::update(int screenWidth, int screenHeight) {
    updateLayout(screenWidth, screenHeight);
    clampSelection();

    float mouseX = 0.0f;
    float mouseY = 0.0f;
    Input::getMousePosFramebuffer(mouseX, mouseY, screenWidth, screenHeight);

    if (hasModal()) {
        updateModalLayout(screenWidth, screenHeight);
        updateModalTextInput(mouseX);
        updateModalButtons(mouseX, mouseY);
        return;
    }

    updateTextInput(mouseX);
    updateSelection(mouseX, mouseY);
    updateButtons(mouseX, mouseY);
}

// --- 9. Rendering ---

void hudWorldLauncher::drawRect(Shader& shader, const glm::ivec4& rect, const glm::vec4& color) const {
    if (rect.z <= 0 || rect.w <= 0) {
        return;
    }

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(static_cast<float>(rect.x), static_cast<float>(rect.y), 0.0f));
    model = glm::scale(model, glm::vec3(static_cast<float>(rect.z), static_cast<float>(rect.w), 1.0f));

    shader.setMat4("model", model);
    shader.setInt("isText", 0);
    shader.setVec4("color", color);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, HUD::getWhiteTexture());
    HUD::bindQuad();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    HUD::unbindQuad();
}

void hudWorldLauncher::drawCenteredText(hudText& text, const std::string& value, const glm::ivec4& rect,
    int screenWidth, int screenHeight, Shader& shader) const {
    text.setText(value);
    const glm::vec2 size = text.measure();

    const int x = rect.x + static_cast<int>(std::round((rect.z - size.x) * 0.5f));
    const int y = rect.y + static_cast<int>(std::round((rect.w - size.y) * 0.5f));

    text.setPosition(x, y);
    text.draw(shader, screenWidth, screenHeight);
}

void hudWorldLauncher::draw(Shader& shader, int screenWidth, int screenHeight) {
    // --- Frame Initialization ---
    drawCaret_ = false;
    drawSelection_ = false;
    caretPixelOffset_ = 0.0f;
    selectionPixelStart_ = 0.0f;
    selectionPixelEnd_ = 0.0f;

    const float panelWidth = clampFloat(screenWidth * kPanelWidthRatio, kPanelMinWidth, kPanelMaxWidth);
    const float panelHeight = clampFloat(screenHeight * kPanelHeightRatio, kPanelMinHeight, kPanelMaxHeight);
    const int panelX = static_cast<int>(std::round((screenWidth - panelWidth) * 0.5f));
    const int panelY = static_cast<int>(std::round((screenHeight - panelHeight) * 0.5f));

    const glm::ivec4 panelRect(
        panelX, panelY,
        static_cast<int>(std::round(panelWidth)),
        static_cast<int>(std::round(panelHeight))
    );

    // --- Draw Main Backgrounds ---
    drawRect(shader, glm::ivec4(0, 0, screenWidth, screenHeight), glm::vec4(0.04f, 0.04f, 0.05f, 1.0f));
    drawRect(shader, expandRect(panelRect, 4), glm::vec4(0.18f, 0.21f, 0.32f, 1.0f));
    drawRect(shader, panelRect, glm::vec4(0.07f, 0.08f, 0.11f, 1.0f));

    // --- Draw Headers ---
    titleText_->setPosition(panelRect.x + kOuterPadding, panelRect.y + kOuterPadding);
    titleText_->draw(shader, screenWidth, screenHeight);

    subtitleText_->setPosition(panelRect.x + kOuterPadding, panelRect.y + kOuterPadding + 28);
    subtitleText_->draw(shader, screenWidth, screenHeight);

    // --- Draw World List Box ---
    drawRect(shader, expandRect(layout_.listRect, 4), glm::vec4(0.18f, 0.21f, 0.32f, 1.0f));
    drawRect(shader, layout_.listRect, glm::vec4(0.08f, 0.09f, 0.12f, 1.0f));

    const int rowWidth = layout_.listRect.z - kListInnerPadding * 2 - kListScrollbarWidth - kListScrollbarMargin;
    const int firstRowY = layout_.listRect.y + kListInnerPadding;
    const int startIndex = scrollOffset_;
    const int endIndex = std::min(startIndex + layout_.visibleRows, static_cast<int>(worlds_.size()));

    float mouseX = 0.0f;
    float mouseY = 0.0f;
    Input::getMousePosFramebuffer(mouseX, mouseY, screenWidth, screenHeight);

    if (worlds_.empty()) {
        emptyText_->setPosition(layout_.listRect.x + kListInnerPadding + 6, layout_.listRect.y + kListInnerPadding + 6);
        emptyText_->draw(shader, screenWidth, screenHeight);
    }
    else {
        // --- Draw World Items ---
        for (int index = startIndex; index < endIndex; ++index) {
            const auto& summary = worlds_[static_cast<std::size_t>(index)];
            const int rowOffset = index - startIndex;
            const glm::ivec4 rowRect(
                layout_.listRect.x + kListInnerPadding,
                firstRowY + static_cast<int>(std::round(rowOffset * (layout_.rowHeight + layout_.rowGap))),
                rowWidth,
                static_cast<int>(std::round(layout_.rowHeight))
            );

            const bool selected = index == selectedIndex_;
            const bool hovered = index == hoveredIndex_;

            const glm::vec4 rowColor = hovered ? glm::vec4(0.14f, 0.15f, 0.20f, 1.0f)
                : selected ? glm::vec4(0.12f, 0.13f, 0.18f, 1.0f)
                : glm::vec4(0.09f, 0.10f, 0.14f, 1.0f);

            const glm::vec4 borderColor = selected ? glm::vec4(0.38f, 0.44f, 0.66f, 1.0f)
                : glm::vec4(0.03f, 0.04f, 0.06f, 1.0f);

            drawRect(shader, expandRect(rowRect, 2), borderColor);
            drawRect(shader, rowRect, rowColor);

            rowTitleText_->setColor(glm::vec3(0.97f, 0.98f, 1.0f));
            rowTitleText_->setText(summary.manifest.displayName);
            rowTitleText_->setPosition(rowRect.x + 12, rowRect.y + 12);
            rowTitleText_->draw(shader, screenWidth, screenHeight);

            rowDetailText_->setColor(glm::vec3(0.68f, 0.72f, 0.80f));
            rowDetailText_->setText(
                "Seed: " + formatSeed(summary.manifest.rootSeed) +
                " | Playtime: " +
                formatPlaytime(summary.gameplayStats.playtimeSeconds)
            );
            rowDetailText_->setPosition(rowRect.x + 12, rowRect.y + 40);
            rowDetailText_->draw(shader, screenWidth, screenHeight);
        }

        // --- Draw Scrollbar ---
        if (static_cast<int>(worlds_.size()) > layout_.visibleRows) {
            const glm::ivec4 trackRect(
                layout_.listRect.x + layout_.listRect.z - kListScrollbarWidth - kListScrollbarMargin,
                layout_.listRect.y + kListInnerPadding,
                kListScrollbarWidth,
                layout_.listRect.w - kListInnerPadding * 2
            );
            drawRect(shader, trackRect, glm::vec4(0.15f, 0.18f, 0.28f, 1.0f));

            const float visibleRatio = static_cast<float>(layout_.visibleRows) / static_cast<float>(worlds_.size());
            const float thumbHeight = std::max(32.0f, trackRect.w == 0 ? 0.0f : trackRect.w * visibleRatio);
            const int maxScroll = std::max(1, static_cast<int>(worlds_.size()) - layout_.visibleRows);
            const float scrollAlpha = static_cast<float>(scrollOffset_) / static_cast<float>(maxScroll);
            const float thumbTravel = std::max(0.0f, static_cast<float>(trackRect.w) - thumbHeight);
            const int thumbWidth = std::max(6, trackRect.z - 6);

            const glm::ivec4 thumbRect(
                trackRect.x + 3,
                trackRect.y + static_cast<int>(std::round(thumbTravel * scrollAlpha)),
                thumbWidth,
                static_cast<int>(std::round(thumbHeight))
            );
            drawRect(shader, thumbRect, glm::vec4(0.55f, 0.60f, 0.84f, 0.95f));
        }
    }

    // --- Draw Input Field ---
    const bool inputHovered = pointInRect(mouseX, mouseY, layout_.inputRect);

    drawRect(shader, expandRect(layout_.inputRect, 2),
        textFieldFocused_ ? glm::vec4(0.86f, 0.80f, 0.40f, 1.0f)
        : glm::vec4(0.18f, 0.21f, 0.32f, 1.0f));

    drawRect(shader, layout_.inputRect,
        glm::vec4(0.08f, 0.09f, 0.12f, inputHovered ? 1.0f : 0.96f));

    const bool showPlaceholder = worldNameInput_.empty();
    inputText_->setColor(showPlaceholder ? glm::vec3(0.45f, 0.42f, 0.28f)
        : glm::vec3(1.0f, 0.95f, 0.6f));
    inputText_->setText(showPlaceholder ? "New World" : worldNameInput_.text);

    if (!showPlaceholder) {
        caretPixelOffset_ = inputText_->measureText(worldNameInput_.text.substr(0, worldNameInput_.caretIndex)).x;
        drawCaret_ = textFieldFocused_ && std::fmod(ENGINE::GETTIME(), 1.0) < 0.5;

        if (worldNameInput_.hasSelection()) {
            const std::size_t first = std::min(worldNameInput_.selectionStart, worldNameInput_.selectionEnd);
            const std::size_t last = std::max(worldNameInput_.selectionStart, worldNameInput_.selectionEnd);

            selectionPixelStart_ = inputText_->measureText(worldNameInput_.text.substr(0, first)).x;
            selectionPixelEnd_ = inputText_->measureText(worldNameInput_.text.substr(0, last)).x;
            drawSelection_ = textFieldFocused_ && selectionPixelEnd_ > selectionPixelStart_;
        }
    }
    else {
        clearSelection();
        drawCaret_ = textFieldFocused_ && std::fmod(ENGINE::GETTIME(), 1.0) < 0.5;
    }

    inputText_->setPosition(layout_.inputRect.x + kInputPadding, layout_.inputRect.y + 8);

    if (drawSelection_) {
        drawRect(shader,
            glm::ivec4(inputText_->getX() + static_cast<int>(std::round(selectionPixelStart_)),
                inputText_->getY(),
                static_cast<int>(std::round(selectionPixelEnd_ - selectionPixelStart_)),
                static_cast<int>(std::round(inputText_->measure().y))),
            glm::vec4(0.3f, 0.45f, 0.9f, 0.65f));
    }

    inputText_->draw(shader, screenWidth, screenHeight);

    if (drawCaret_) {
        drawRect(shader,
            glm::ivec4(inputText_->getX() + static_cast<int>(std::round(caretPixelOffset_)),
                inputText_->getY(),
                static_cast<int>(std::round(kCaretWidth)),
                static_cast<int>(std::round(inputText_->measure().y))),
            glm::vec4(1.0f, 0.9f, 0.2f, 1.0f));
    }

    // --- Draw Action Buttons ---
    const bool actionHovered = pointInRect(mouseX, mouseY, layout_.actionButtonRect);
    const bool renameHovered = pointInRect(mouseX, mouseY, layout_.renameButtonRect);
    const bool deleteHovered = pointInRect(mouseX, mouseY, layout_.deleteButtonRect);
    const bool exitHovered = pointInRect(mouseX, mouseY, layout_.exitButtonRect);
    const bool renameEnabled = selectedIndex_ >= 0;
    const bool deleteEnabled = selectedIndex_ >= 0;

    const std::string actionLabel = selectedIndex_ >= 0 ? "Load" : "Generate";

    drawRect(shader, expandRect(layout_.actionButtonRect, 2), glm::vec4(0.18f, 0.21f, 0.32f, 1.0f));
    drawRect(shader, layout_.actionButtonRect,
        actionHovered && !loading_ ? glm::vec4(0.15f, 0.17f, 0.25f, 1.0f)
        : glm::vec4(0.12f, 0.14f, 0.21f, 1.0f));

    actionButtonText_->setColor(glm::vec3(0.95f, 0.97f, 1.0f));
    drawCenteredText(*actionButtonText_, actionLabel, layout_.actionButtonRect, screenWidth, screenHeight, shader);

    drawRect(shader, expandRect(layout_.renameButtonRect, 2), glm::vec4(0.18f, 0.21f, 0.32f, 1.0f));
    drawRect(shader, layout_.renameButtonRect,
        renameEnabled
            ? (renameHovered && !loading_ ? glm::vec4(0.16f, 0.16f, 0.24f, 1.0f)
                                          : glm::vec4(0.12f, 0.13f, 0.20f, 1.0f))
            : glm::vec4(0.10f, 0.10f, 0.12f, 1.0f));

    renameButtonText_->setColor(
        renameEnabled ? glm::vec3(0.92f, 0.94f, 1.0f)
                      : glm::vec3(0.46f, 0.44f, 0.48f)
    );
    drawCenteredText(*renameButtonText_, "Rename", layout_.renameButtonRect, screenWidth, screenHeight, shader);

    drawRect(shader, expandRect(layout_.deleteButtonRect, 2), glm::vec4(0.18f, 0.21f, 0.32f, 1.0f));
    drawRect(shader, layout_.deleteButtonRect,
        deleteEnabled
            ? (deleteHovered && !loading_ ? glm::vec4(0.18f, 0.11f, 0.13f, 1.0f)
                                          : glm::vec4(0.13f, 0.08f, 0.10f, 1.0f))
            : glm::vec4(0.10f, 0.10f, 0.12f, 1.0f));

    deleteButtonText_->setColor(
        deleteEnabled ? glm::vec3(0.98f, 0.90f, 0.90f)
                      : glm::vec3(0.46f, 0.44f, 0.48f)
    );
    drawCenteredText(*deleteButtonText_, "Delete", layout_.deleteButtonRect, screenWidth, screenHeight, shader);

    drawRect(shader, expandRect(layout_.exitButtonRect, 2), glm::vec4(0.18f, 0.21f, 0.32f, 1.0f));
    drawRect(shader, layout_.exitButtonRect,
        exitHovered && !loading_ ? glm::vec4(0.17f, 0.11f, 0.13f, 1.0f)
        : glm::vec4(0.12f, 0.09f, 0.11f, 1.0f));

    exitButtonText_->setColor(glm::vec3(0.98f, 0.90f, 0.90f));
    drawCenteredText(*exitButtonText_, "Exit Game", layout_.exitButtonRect, screenWidth, screenHeight, shader);

    // --- Draw Status Texts & Overlays ---
    if (!statusMessage_.empty()) {
        statusText_->setText(statusMessage_);
        statusText_->setPosition(panelRect.x + kOuterPadding, layout_.exitButtonRect.y + layout_.exitButtonRect.w + 12);
        statusText_->draw(shader, screenWidth, screenHeight);
    }

    if (loading_) {
        drawRect(shader, glm::ivec4(0, 0, screenWidth, screenHeight), glm::vec4(0.01f, 0.01f, 0.02f, 0.84f));

        const glm::ivec4 loadingRect(panelRect.x, panelRect.y + panelRect.w / 2 - 70, panelRect.z, 140);

        loadingText_->setPosition(
            loadingRect.x + static_cast<int>(std::round((loadingRect.z - loadingText_->measure().x) * 0.5f)),
            loadingRect.y + 20
        );
        loadingText_->draw(shader, screenWidth, screenHeight);

        const int dotCount = 1 + static_cast<int>(std::fmod(ENGINE::GETTIME() * 3.0, 4.0));
        loadingDotsText_->setText(std::string(static_cast<std::size_t>(dotCount), '.'));

        loadingDotsText_->setPosition(
            loadingRect.x + static_cast<int>(std::round((loadingRect.z - loadingDotsText_->measure().x) * 0.5f)),
            loadingRect.y + 58
        );
        loadingDotsText_->draw(shader, screenWidth, screenHeight);
    }

    if (hasModal()) {
        const bool renameModal = modalType_ == ModalType::RenameWorld;
        const bool confirmHovered =
            pointInRect(mouseX, mouseY, modalLayout_.confirmButtonRect);
        const bool cancelHovered =
            pointInRect(mouseX, mouseY, modalLayout_.cancelButtonRect);

        drawRect(shader, glm::ivec4(0, 0, screenWidth, screenHeight),
                 glm::vec4(0.01f, 0.01f, 0.02f, 0.72f));
        drawRect(shader, expandRect(modalLayout_.panelRect, 3),
                 glm::vec4(0.18f, 0.21f, 0.32f, 1.0f));
        drawRect(shader, modalLayout_.panelRect,
                 glm::vec4(0.07f, 0.08f, 0.11f, 0.98f));

        modalTitleText_->setText(
            renameModal ? "Rename World" : "Delete World?"
        );
        modalTitleText_->setPosition(
            modalLayout_.titleRect.x, modalLayout_.titleRect.y
        );
        modalTitleText_->draw(shader, screenWidth, screenHeight);

        if (modalWorldIndex_ >= 0 &&
            modalWorldIndex_ < static_cast<int>(worlds_.size())) {
            const auto& world =
                worlds_[static_cast<std::size_t>(modalWorldIndex_)];
            modalBodyText_->setText(
                renameModal
                    ? "Update the world name and world folder for \"" +
                          world.manifest.displayName + "\"."
                    : "This permanently deletes \"" +
                          world.manifest.displayName + "\" from disk."
            );
        } else {
            modalBodyText_->setText("");
        }
        modalBodyText_->setPosition(
            modalLayout_.bodyRect.x, modalLayout_.bodyRect.y
        );
        modalBodyText_->draw(shader, screenWidth, screenHeight);

        if (renameModal) {
            drawRect(shader, expandRect(modalLayout_.inputRect, 2),
                     glm::vec4(0.86f, 0.80f, 0.40f, 1.0f));
            drawRect(shader, modalLayout_.inputRect,
                     glm::vec4(0.08f, 0.09f, 0.12f, 0.98f));

            modalInputText_->setColor(glm::vec3(0.95f, 0.97f, 1.0f));
            modalInputText_->setText(modalWorldNameInput_.text);

            modalCaretPixelOffset_ = modalInputText_->measureText(
                modalWorldNameInput_.text.substr(0, modalWorldNameInput_.caretIndex)
            ).x;
            modalDrawCaret_ = std::fmod(ENGINE::GETTIME(), 1.0) < 0.5;

            if (modalWorldNameInput_.hasSelection()) {
                const std::size_t first = std::min(
                    modalWorldNameInput_.selectionStart,
                    modalWorldNameInput_.selectionEnd
                );
                const std::size_t last = std::max(
                    modalWorldNameInput_.selectionStart,
                    modalWorldNameInput_.selectionEnd
                );

                modalSelectionPixelStart_ = modalInputText_->measureText(
                    modalWorldNameInput_.text.substr(0, first)
                ).x;
                modalSelectionPixelEnd_ = modalInputText_->measureText(
                    modalWorldNameInput_.text.substr(0, last)
                ).x;
                modalDrawSelection_ =
                    modalSelectionPixelEnd_ > modalSelectionPixelStart_;
            }

            modalInputText_->setPosition(
                modalLayout_.inputRect.x + kInputPadding,
                modalLayout_.inputRect.y + 8
            );

            if (modalDrawSelection_) {
                drawRect(shader,
                         glm::ivec4(
                             modalInputText_->getX() + static_cast<int>(std::round(modalSelectionPixelStart_)),
                             modalInputText_->getY(),
                             static_cast<int>(std::round(modalSelectionPixelEnd_ - modalSelectionPixelStart_)),
                             static_cast<int>(std::round(modalInputText_->measure().y))
                         ),
                         glm::vec4(0.3f, 0.45f, 0.9f, 0.65f));
            }

            modalInputText_->draw(shader, screenWidth, screenHeight);

            if (modalDrawCaret_) {
                drawRect(shader,
                         glm::ivec4(
                             modalInputText_->getX() + static_cast<int>(std::round(modalCaretPixelOffset_)),
                             modalInputText_->getY(),
                             static_cast<int>(std::round(kCaretWidth)),
                             static_cast<int>(std::round(modalInputText_->measure().y))
                         ),
                         glm::vec4(1.0f, 0.9f, 0.2f, 1.0f));
            }
        }

        drawRect(shader, expandRect(modalLayout_.confirmButtonRect, 2),
                 glm::vec4(0.18f, 0.21f, 0.32f, 1.0f));
        drawRect(shader, modalLayout_.confirmButtonRect,
                 renameModal
                     ? (confirmHovered ? glm::vec4(0.15f, 0.17f, 0.25f, 1.0f)
                                       : glm::vec4(0.12f, 0.14f, 0.21f, 1.0f))
                     : (confirmHovered ? glm::vec4(0.18f, 0.11f, 0.13f, 1.0f)
                                       : glm::vec4(0.13f, 0.08f, 0.10f, 1.0f)));

        modalConfirmButtonText_->setColor(
            renameModal ? glm::vec3(0.95f, 0.97f, 1.0f)
                        : glm::vec3(0.98f, 0.90f, 0.90f)
        );
        drawCenteredText(
            *modalConfirmButtonText_,
            renameModal ? "Save" : "Delete",
            modalLayout_.confirmButtonRect,
            screenWidth,
            screenHeight,
            shader
        );

        drawRect(shader, expandRect(modalLayout_.cancelButtonRect, 2),
                 glm::vec4(0.18f, 0.21f, 0.32f, 1.0f));
        drawRect(shader, modalLayout_.cancelButtonRect,
                 cancelHovered ? glm::vec4(0.15f, 0.16f, 0.22f, 1.0f)
                               : glm::vec4(0.11f, 0.12f, 0.17f, 1.0f));

        modalCancelButtonText_->setColor(glm::vec3(0.95f, 0.97f, 1.0f));
        drawCenteredText(*modalCancelButtonText_, "Cancel",
                         modalLayout_.cancelButtonRect, screenWidth,
                         screenHeight, shader);
    }
}
