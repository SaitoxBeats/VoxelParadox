#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/support/text_input.hpp"
#include "hud_element.hpp"
#include "world/persistence/world_save_service.hpp"

class hudText;

class hudWorldLauncher : public HUDElement {
public:
    enum class ActionType {
        None = 0,
        CreateWorld,
        LoadWorld,
        RenameWorld,
        DeleteWorld,
        ExitGame,
    };

    struct ActionRequest {
        ActionType type = ActionType::None;
        std::filesystem::path worldDirectory{};
        std::string worldName{};
    };

    explicit hudWorldLauncher(const std::string& fontPath = "");
    ~hudWorldLauncher() override;

    void setWorlds(const std::vector<WorldSaveService::WorldSummary>& worlds);
    void setLoading(bool loading);
    void setStatusMessage(const std::string& statusMessage);
    void setSelectedWorldDirectory(const std::filesystem::path& worldDirectory);

    ActionRequest consumeRequest();

    void update(int screenWidth, int screenHeight) override;
    void draw(class Shader& shader, int screenWidth, int screenHeight) override;

private:
    struct LayoutMetrics {
        glm::ivec4 listRect{0};
        glm::ivec4 inputRect{0};
        glm::ivec4 actionButtonRect{0};
        glm::ivec4 renameButtonRect{0};
        glm::ivec4 deleteButtonRect{0};
        glm::ivec4 exitButtonRect{0};
        float rowHeight = 0.0f;
        float rowGap = 0.0f;
        int visibleRows = 0;
    };

    enum class ModalType {
        None = 0,
        ConfirmDelete,
        RenameWorld,
    };

    struct ModalLayout {
        glm::ivec4 panelRect{0};
        glm::ivec4 titleRect{0};
        glm::ivec4 bodyRect{0};
        glm::ivec4 inputRect{0};
        glm::ivec4 confirmButtonRect{0};
        glm::ivec4 cancelButtonRect{0};
    };

    static constexpr std::size_t kMaxWorldNameLength = 48;

    std::vector<WorldSaveService::WorldSummary> worlds_{};
    std::string statusMessage_{};
    ActionRequest pendingRequest_{};
    TextInputState worldNameInput_{};

    int selectedIndex_ = -1;
    int hoveredIndex_ = -1;
    int scrollOffset_ = 0;
    int lastClickedIndex_ = -1;
    double lastClickTime_ = -1.0;
    bool textFieldFocused_ = false;
    bool loading_ = false;
    float caretPixelOffset_ = 0.0f;
    float selectionPixelStart_ = 0.0f;
    float selectionPixelEnd_ = 0.0f;
    bool drawCaret_ = false;
    bool drawSelection_ = false;
    ModalType modalType_ = ModalType::None;
    ModalLayout modalLayout_{};
    TextInputState modalWorldNameInput_{};
    int modalWorldIndex_ = -1;
    float modalCaretPixelOffset_ = 0.0f;
    float modalSelectionPixelStart_ = 0.0f;
    float modalSelectionPixelEnd_ = 0.0f;
    bool modalDrawCaret_ = false;
    bool modalDrawSelection_ = false;

    LayoutMetrics layout_{};

    hudText* titleText_ = nullptr;
    hudText* subtitleText_ = nullptr;
    hudText* rowTitleText_ = nullptr;
    hudText* rowDetailText_ = nullptr;
    hudText* emptyText_ = nullptr;
    hudText* inputText_ = nullptr;
    hudText* actionButtonText_ = nullptr;
    hudText* renameButtonText_ = nullptr;
    hudText* deleteButtonText_ = nullptr;
    hudText* exitButtonText_ = nullptr;
    hudText* statusText_ = nullptr;
    hudText* loadingText_ = nullptr;
    hudText* loadingDotsText_ = nullptr;
    hudText* modalTitleText_ = nullptr;
    hudText* modalBodyText_ = nullptr;
    hudText* modalInputText_ = nullptr;
    hudText* modalConfirmButtonText_ = nullptr;
    hudText* modalCancelButtonText_ = nullptr;

    void updateLayout(int screenWidth, int screenHeight);
    void updateTextInput(float mouseX);
    void updateModalLayout(int screenWidth, int screenHeight);
    void updateModalTextInput(float mouseX);
    void updateSelection(float mouseX, float mouseY);
    void updateButtons(float mouseX, float mouseY);
    void updateModalButtons(float mouseX, float mouseY);
    void clampSelection();
    void requestCreateWorld();
    void requestLoadWorld(int index);
    void requestRenameWorld(int index);
    void requestDeleteWorld(int index);
    void beginDeleteConfirmation(int index);
    void beginRenameWorld(int index);
    void closeModal();
    bool hasModal() const;
    bool hasSelection() const;
    void clearSelection();
    void selectAll();
    void deleteSelection();
    void insertText(const std::string& typed);
    void moveCaretLeft();
    void moveCaretRight();
    void eraseBackward();
    void eraseForward();
    bool consumeHeldKey(int key, double now, double& nextRepeatTime);
    void placeCaretFromMouse(float mouseX);
    void placeModalCaretFromMouse(float mouseX);

    int hoveredWorldIndex(float mouseX, float mouseY) const;
    bool pointInRect(float x, float y, const glm::ivec4& rect) const;

    void drawRect(class Shader& shader, const glm::ivec4& rect,
                  const glm::vec4& color) const;
    void drawCenteredText(hudText& text, const std::string& value,
                          const glm::ivec4& rect, int screenWidth,
                          int screenHeight, class Shader& shader) const;
};
