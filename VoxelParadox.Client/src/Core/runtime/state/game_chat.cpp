// game_chat.cpp
// Unity mental model: HUD and Controller for the in-game chat.
// Handles opening/closing, capturing text input, autocomplete suggestions, and command execution.

#pragma region Includes

// 1. Standard Library
#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// 2. Third-party Libraries
#include <GLFW/glfw3.h>

// 3. Internal Engine/Core Modules
#include "engine/engine.hpp"
#include "engine/input.hpp"
#include "input/input_action_ids.hpp"
#include "input/input_action_system.hpp"

// 4. Local Project Modules
#include "runtime/state/game_chat.hpp"
#include "enemies/enemy_spawn_system.hpp"
#include "items/item_catalog.hpp"
#include "player/player.hpp"
#include "render/hud/hud_chat_background.hpp"
#include "render/hud/hud.hpp"
#include "render/hud/hud_text.hpp"
#include "render/hud/hud_watch_text.hpp"
#include "runtime/ui/runtime_hud_ids.hpp"
#include "world/persistence/world_stack.hpp"

#pragma endregion

namespace {

#pragma region 1. Constants & Local Helpers
    // --- 1. Constants & Local Helpers ---
    // Chat stays self-contained on purpose: open/close, text capture, suggestions,
    // and HUD visibility all live in one file.

    constexpr const char* kChatHistoryGroup = RuntimeHudIds::kChatHistory;
    constexpr const char* kChatBackgroundGroup = "runtime.chat.background";
    constexpr const char* kChatInputGroup = RuntimeHudIds::kChatInput;
    constexpr const char* kChatSuggestionGroup = RuntimeHudIds::kChatSuggestions;

    constexpr double kClosedHistoryLifetimeSeconds = 8.0;
    constexpr float kChatLeftMargin = 22.0f;
    constexpr float kChatInputBottomMargin = 20.0f;
    constexpr float kChatHistoryBottomMargin = 64.0f;
    constexpr float kChatLineSpacing = 24.0f;
    constexpr float kChatSuggestionLineSpacing = 22.0f;
    constexpr float kChatSuggestionBottomMargin = 60.0f;
    constexpr std::size_t kChatMaxCharactersPerLine = 54;
    constexpr float kChatInputRightMargin = 12.0f;
    constexpr float kChatInputHorizontalPadding = 12.0f;
    constexpr float kChatInputBackgroundLeftMargin = 12.0f;
    constexpr float kChatInputBackgroundBottomMargin = 14.0f;
    constexpr float kChatInputBackgroundHeight = 42.0f;
    constexpr float kChatSelectionHeightPadding = 2.0f;
} // namespace

namespace GameChatTheme {

const glm::vec3 kDefaultHistoryColor(0.92f, 0.95f, 1.0f);
const glm::vec3 kVersalNotificationColor(1.0f, 0.82f, 0.36f);

} // namespace GameChatTheme

namespace {

    const glm::vec3 kChatDefaultHistoryColor = GameChatTheme::kDefaultHistoryColor;
    const glm::vec3 kVersalNotificationColor = GameChatTheme::kVersalNotificationColor;

    bool isSameColor(const glm::vec3& lhs, const glm::vec3& rhs) {
        return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
    }

    void appendHistorySegment(std::vector<GameChatTextSegment>& segments,
                              GameChatTextSegment segment) {
        if (segment.text.empty()) {
            return;
        }

        if (!segments.empty() && isSameColor(segments.back().color, segment.color)) {
            segments.back().text += segment.text;
            return;
        }

        segments.push_back(std::move(segment));
    }

    glm::ivec4 makeChatInputRect(int screenWidth, int screenHeight) {
        const int x = static_cast<int>(kChatInputBackgroundLeftMargin);
        const int y = static_cast<int>(screenHeight - kChatInputBackgroundBottomMargin -
                                       kChatInputBackgroundHeight);
        const int width = std::max(
            0,
            screenWidth - x - static_cast<int>(kChatInputRightMargin)
        );

        return glm::ivec4(
            x,
            y,
            width,
            static_cast<int>(kChatInputBackgroundHeight)
        );
    }

    template <typename T>
    T* addGroupedHudElement(T* element, const char* groupName, int layer = 0) {
        if (element) {
            element->setLayer(layer);
            HUD::assignToGroup(element, groupName);
        }
        return element;
    }

    std::vector<std::string> splitWhitespace(const std::string& value) {
        std::istringstream stream(value);
        std::vector<std::string> tokens;
        std::string token;

        while (stream >> token) {
            tokens.push_back(token);
        }
        return tokens;
    }

    std::string lowercaseAscii(std::string value) {
        std::transform(
            value.begin(), value.end(), value.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); }
        );
        return value;
    }

    std::string stripInventoryPrefix(const std::string& rawValue) {
        const std::string normalized = lowercaseAscii(rawValue);

        if (normalized.rfind("block:", 0) == 0) {
            return rawValue.substr(6);
        }

        if (normalized.rfind("item:", 0) == 0) {
            return rawValue.substr(5);
        }

        return rawValue;
    }

    std::string displayTextForSuggestion(const std::string& suggestion) {
        if (suggestion == "/get ") {
            return "/get <item> [amount]";
        }
        if (suggestion == "/summon entity:guy ") {
            return "/summon entity:guy <normal|lookat>";
        }
        if (suggestion == "/sandbox ") {
            return "/sandbox [true|false]";
        }
        if (suggestion == "/debug wireframe ") {
            return "/debug wireframe <true|false>";
        }
        if (suggestion == "/debug camera:culling ") {
            return "/debug camera:culling <true|false>";
        }
    if (suggestion == "/debug world:mesh ") {
        return "/debug world:mesh <naive|greedy>";
    }
    if (suggestion == "/set level ") {
        return "/set level <num>";
    }
    if (suggestion == "/set xp ") {
        return "/set xp <num>";
    }
    if (suggestion == "/reset level") {
        return "/reset level";
    }
    if (suggestion == "/reset xp") {
        return "/reset xp";
    }
    if (suggestion == "/clear") {
        return "/clear";
    }
    return suggestion;
}

    bool tryGetCurrentLookTarget(const Player& player, FractalWorld& world,
        glm::ivec3& outTargetBlock, glm::ivec3& outTargetNormal) {
        const glm::vec3 origin = player.camera.position;
        const glm::vec3 dir = player.camera.getForward();

        glm::ivec3 current = glm::ivec3(glm::floor(origin));
        glm::ivec3 step(0);
        glm::vec3 tMax(0.0f);
        glm::vec3 tDelta(0.0f);

        for (int axis = 0; axis < 3; axis++) {
            if (dir[axis] > 0.0f) {
                step[axis] = 1;
                tMax[axis] = (std::floor(origin[axis]) + 1.0f - origin[axis]) / dir[axis];
                tDelta[axis] = 1.0f / dir[axis];
            }
            else if (dir[axis] < 0.0f) {
                step[axis] = -1;
                tMax[axis] = (origin[axis] - std::floor(origin[axis])) / (-dir[axis]);
                tDelta[axis] = 1.0f / (-dir[axis]);
            }
            else {
                step[axis] = 0;
                tMax[axis] = 1e30f;
                tDelta[axis] = 1e30f;
            }
        }

        const int maxSteps = static_cast<int>(player.breakRange / 0.5f);

        for (int stepIndex = 0; stepIndex < maxSteps; stepIndex++) {
            int axis = 0;

            if (tMax.x < tMax.y) {
                axis = tMax.x < tMax.z ? 0 : 2;
            }
            else {
                axis = tMax.y < tMax.z ? 1 : 2;
            }

            current[axis] += step[axis];
            const float t = tMax[axis];
            tMax[axis] += tDelta[axis];

            if (t > player.breakRange) {
                break;
            }

            if (!canTargetBlock(world.getBlock(current))) {
                continue;
            }

            outTargetBlock = current;
            outTargetNormal = glm::ivec3(0);
            outTargetNormal[axis] = -step[axis];
            return true;
        }

        return false;
    }

    bool trySummonEnemyAtLookTarget(GameChatCommandContext& commandContext,
        EnemyType type, std::string& outFailureReason) {
        FractalWorld* world = commandContext.worldStack.currentWorld();

        if (!world) {
            outFailureReason = "there is no active world.";
            return false;
        }

        glm::ivec3 targetBlock(0);
        glm::ivec3 targetNormal(0);

        if (!tryGetCurrentLookTarget(commandContext.player, *world, targetBlock, targetNormal) ||
            targetNormal != glm::ivec3(0, 1, 0)) {
            outFailureReason = "look at the top face of a solid block first.";
            return false;
        }

        const BlockId supportType = world->getBlock(targetBlock);
        if (!isSolid(supportType)) {
            outFailureReason = "look at the top face of a solid block first.";
            return false;
        }

        const glm::vec3 spawnPosition = glm::vec3(targetBlock) + glm::vec3(0.5f, 1.0f, 0.5f);
        const glm::vec3 forward = glm::normalize(commandContext.player.camera.getForward());
        const float yawDegrees = glm::degrees(std::atan2(-forward.x, -forward.z));

        return tryForceSpawnWorldEnemyAt(
            *world, type, spawnPosition, yawDegrees, &outFailureReason
        );
    }

#pragma endregion

} // namespace

#pragma region 2. Lifecycle & Core Input
// --- 2. Lifecycle & Core Input ---

void GameChat::open() {
    if (open_) {
        return;
    }

    open_ = true;
    historyScrollOffset_ = 0;
    historyScrollRemainder_ = 0.0f;
    inputState_.resetRepeats();
    inputState_.endMouseSelection();
    Input::enableTextInput(true);
    Input::setCursorVisible(true);
}

void GameChat::close() {
    if (!open_) {
        return;
    }

    open_ = false;
    historyScrollOffset_ = 0;
    historyScrollRemainder_ = 0.0f;
    inputState_.setText({});
    inputState_.resetRepeats();
    inputState_.endMouseSelection();
    Input::enableTextInput(false);
    Input::setCursorVisible(false);
}

void GameChat::pushNotification(std::vector<GameChatTextSegment> segments) {
    pushHistory(std::move(segments));
}

void GameChat::pushNotification(std::string text) {
    pushHistory(text);
}

void GameChat::pushFirstVersalNotification() {
    pushNotification({
        { "You've levelled up! You've earned a ", kChatDefaultHistoryColor },
        { "versal", kVersalNotificationColor },
        { ".", kChatDefaultHistoryColor },
        { " ", kChatDefaultHistoryColor },
        { "Right-click on a block to open a ", kChatDefaultHistoryColor },
        { "portal! ", glm::vec3(0.86f, 0.65f, 2.42f) }
    });
}

bool GameChat::handleFrameInput(GameChatCommandContext& commandContext, bool allowOpenChat) {
    if (!open_) {
        if (!allowOpenChat ||
            !InputMapping::InputActionSystem::instance().wasPressed(InputActionIds::kOpenChat)) {
            return false;
        }

        open();
        return true;
    }

    const double now = ENGINE::GETTIME();

    const float scrollDelta = Input::getScroll();
    if (scrollDelta != 0.0f) {
        historyScrollRemainder_ += scrollDelta;

        const int scrollLines = static_cast<int>(historyScrollRemainder_);
        if (scrollLines != 0) {
            const int appliedScroll = adjustHistoryScroll(scrollLines);
            if (appliedScroll == scrollLines) {
                historyScrollRemainder_ -= static_cast<float>(scrollLines);
            }
            else {
                historyScrollRemainder_ = 0.0f;
            }
        }
    }

    if (Input::mousePressed(GLFW_MOUSE_BUTTON_LEFT)) {
        float mouseX = 0.0f;
        float mouseY = 0.0f;
        const glm::vec2 viewportSize = ENGINE::GETVIEWPORTSIZE();
        Input::getMousePosFramebuffer(
            mouseX,
            mouseY,
            static_cast<int>(viewportSize.x),
            static_cast<int>(viewportSize.y)
        );

        if (isMouseInsideInput(mouseX, mouseY)) {
            beginMouseSelection(mouseX);
        }
    }

    if (inputState_.mouseSelecting) {
        if (Input::mouseDown(GLFW_MOUSE_BUTTON_LEFT)) {
            float mouseX = 0.0f;
            float mouseY = 0.0f;
            const glm::vec2 viewportSize = ENGINE::GETVIEWPORTSIZE();
            Input::getMousePosFramebuffer(
                mouseX,
                mouseY,
                static_cast<int>(viewportSize.x),
                static_cast<int>(viewportSize.y)
            );
            updateMouseSelection(mouseX);
        }
        else {
            endMouseSelection();
        }
    }

    inputState_.handleKeyboardEditing(now);

    if (Input::keyPressed(GLFW_KEY_TAB)) {
        autocompleteInput();
        return true;
    }

    if (Input::keyPressed(GLFW_KEY_UP) && !lastSubmittedInput_.empty()) {
        inputState_.setText(lastSubmittedInput_);
        return true;
    }

    if (Input::keyPressed(GLFW_KEY_ESCAPE)) {
        close();
        return true;
    }

    if (Input::keyPressed(GLFW_KEY_ENTER) || Input::keyPressed(GLFW_KEY_KP_ENTER)) {
        submit(commandContext);
        return true;
    }

    return true;
}

#pragma endregion

#pragma region 3. HUD Setup & Sync
// --- 3. HUD Setup & Sync ---

void GameChat::setupHud() {
    HUD::createGroup(kChatBackgroundGroup, 69, false);
    HUD::createGroup(kChatHistoryGroup, 70, false);
    HUD::createGroup(kChatInputGroup, 71, false);
    HUD::createGroup(kChatSuggestionGroup, 72, false);

    auto* background = new hudChatBackground(this);
    HUD::add(background, kChatBackgroundGroup);
    if (background) {
        background->setLayer(-1);
    }

    // Setup History Lines
    for (int lineIndex = 0; lineIndex < kVisibleHistoryLines; lineIndex++) {
        const float yOffset = -(kChatHistoryBottomMargin + kChatLineSpacing * (kVisibleHistoryLines - 1 - lineIndex));

        for (int segmentIndex = 0; segmentIndex < kMaxHistorySegmentsPerLine; segmentIndex++) {
            auto* lineSegment = addGroupedHudElement(
                HUD::watchText(
                    [this, lineIndex, segmentIndex](std::string& out) {
                        out = historyLineSegmentText(lineIndex, segmentIndex);
                    },
                    makeHUDLayout(HUDAnchor::BOTTOM_LEFT, glm::vec2(kChatLeftMargin, yOffset)),
                    glm::vec2(1.0f), 20
                ),
                kChatHistoryGroup,
                lineIndex * kMaxHistorySegmentsPerLine + segmentIndex
            );

            if (lineSegment) {
                lineSegment->setColor(kChatDefaultHistoryColor);
                lineSegment->setVisualBinder(
                    [this, lineIndex, segmentIndex, yOffset](hudWatchText& textElement) {
                        textElement.setColor(historyLineSegmentColor(lineIndex, segmentIndex));
                        textElement.setLayout(
                            makeHUDLayout(
                                HUDAnchor::BOTTOM_LEFT,
                                glm::vec2(
                                    kChatLeftMargin +
                                    historyLineSegmentOffset(lineIndex, segmentIndex, textElement),
                                    yOffset
                                )
                            )
                        );
                    }
                );
            }
        }
    }

    // Setup Input Line
    auto* inputLine = addGroupedHudElement(
        HUD::watchText(
            [this](std::string& out) {
                out = inputLineText();
            },
            makeHUDLayout(HUDAnchor::BOTTOM_LEFT, glm::vec2(kChatLeftMargin, -kChatInputBottomMargin)),
            glm::vec2(1.0f), 22
        ),
        kChatInputGroup, 0
    );

    if (inputLine) {
        inputLine->setColor(glm::vec3(1.0f, 0.98f, 0.72f));
    }

    inputLineElement_ = inputLine;

    // Setup Suggestion Lines
    for (int lineIndex = 0; lineIndex < kVisibleSuggestionLines; lineIndex++) {
        const float yOffset = -(kChatSuggestionBottomMargin + kChatSuggestionLineSpacing * (kVisibleSuggestionLines - 1 - lineIndex));

        auto* suggestionLine = addGroupedHudElement(
            HUD::watchText(
                [this, lineIndex](std::string& out) {
                    out = suggestionLineText(lineIndex);
                },
                makeHUDLayout(HUDAnchor::BOTTOM_LEFT, glm::vec2(kChatLeftMargin, yOffset)),
                glm::vec2(1.0f), 18
            ),
            kChatSuggestionGroup, lineIndex
        );

        if (suggestionLine) {
            suggestionLine->setColor(glm::vec3(0.62f, 0.86f, 1.0f));
        }
    }
}

void GameChat::syncHudState() const {
    HUD::setGroupEnabled(kChatBackgroundGroup, shouldShowHistory() || open_);
    HUD::setGroupEnabled(kChatHistoryGroup, shouldShowHistory());
    HUD::setGroupEnabled(kChatInputGroup, open_);
    HUD::setGroupEnabled(kChatSuggestionGroup, shouldShowSuggestions());
}

#pragma endregion

#pragma region 4. Text Resolution & Formatting
// --- 4. Text Resolution & Formatting ---

std::string GameChat::historyLineText(int lineIndex) const {
    if (lineIndex < 0 || lineIndex >= kVisibleHistoryLines) {
        return {};
    }

    const std::vector<std::string> lines = visibleHistoryLines();
    if (lineIndex >= static_cast<int>(lines.size())) {
        return {};
    }

    return lines[static_cast<std::size_t>(lineIndex)];
}

std::string GameChat::inputLineText() const {
    if (!open_) {
        return {};
    }

    return resolveInputLayout().lineText;
}

std::string GameChat::suggestionLineText(int lineIndex) const {
    if (lineIndex < 0 || lineIndex >= kVisibleSuggestionLines) {
        return {};
    }

    const std::vector<std::string> candidates = autocompleteCandidates();
    if (lineIndex >= static_cast<int>(candidates.size())) {
        return {};
    }

    return displayTextForSuggestion(candidates[static_cast<std::size_t>(lineIndex)]);
}

int GameChat::visibleHistoryLineCount() const {
    return static_cast<int>(visibleHistoryRichLines().size());
}

int GameChat::visibleSuggestionLineCount() const {
    return static_cast<int>(autocompleteCandidates().size());
}

std::vector<GameChat::HistoryLine> GameChat::allHistoryRichLines() const {
    std::vector<HistoryLine> lines;
    lines.reserve(history_.size());

    const double now = ENGINE::GETTIME();
    for (const Entry& entry : history_) {
        if (!open_ && (now - entry.timestampSeconds) > kClosedHistoryLifetimeSeconds) {
            continue;
        }

        const std::vector<HistoryLine> wrappedLines =
            wrapChatSegments(entry.segments, kChatMaxCharactersPerLine);
        lines.insert(lines.end(), wrappedLines.begin(), wrappedLines.end());
    }

    return lines;
}

int GameChat::historyScrollLimit() const {
    const std::vector<HistoryLine> lines = allHistoryRichLines();
    if (lines.size() <= static_cast<std::size_t>(kVisibleHistoryLines)) {
        return 0;
    }

    return static_cast<int>(lines.size() - static_cast<std::size_t>(kVisibleHistoryLines));
}

int GameChat::adjustHistoryScroll(int lineDelta) {
    if (lineDelta == 0) {
        return 0;
    }

    const int previousOffset = historyScrollOffset_;
    historyScrollOffset_ = std::clamp(
        historyScrollOffset_ + lineDelta,
        0,
        historyScrollLimit()
    );
    return historyScrollOffset_ - previousOffset;
}

void GameChat::pushHistory(const std::string& text) {
    if (text.empty()) {
        return;
    }

    pushHistory(std::vector<GameChatTextSegment>{ { text, kChatDefaultHistoryColor } });
}

void GameChat::pushHistory(std::vector<GameChatTextSegment> segments) {
    std::vector<GameChatTextSegment> filteredSegments;
    filteredSegments.reserve(segments.size());

    for (GameChatTextSegment& segment : segments) {
        if (!segment.text.empty()) {
            filteredSegments.push_back(std::move(segment));
        }
    }

    if (filteredSegments.empty()) {
        return;
    }

    const std::vector<HistoryLine> wrappedLines =
        wrapChatSegments(filteredSegments, kChatMaxCharactersPerLine);

    if (open_ && historyScrollOffset_ > 0 && !wrappedLines.empty()) {
        historyScrollOffset_ += static_cast<int>(wrappedLines.size());
    }

    history_.push_back({ std::move(filteredSegments), ENGINE::GETTIME() });
    while (history_.size() > static_cast<std::size_t>(kMaxHistoryEntries)) {
        history_.pop_front();
    }
}

bool GameChat::shouldShowHistory() const {
    return !visibleHistoryRichLines().empty();
}

bool GameChat::shouldShowSuggestions() const {
    return open_ && !autocompleteCandidates().empty();
}

GameChat::ResolvedInputLayout GameChat::resolveInputLayout() const {
    ResolvedInputLayout layout;

    if (!open_) {
        return layout;
    }

    layout.showCaret = (static_cast<int>(ENGINE::GETTIME() * 2.0) % 2) == 0;
    layout.visibleStart = 0;
    layout.visibleEnd = inputState_.text.size();
    layout.lineText = "> " + inputState_.text;

    if (!inputLineElement_) {
        return layout;
    }

    const std::size_t caretIndex = std::min(inputState_.caretIndex, inputState_.text.size());
    const glm::vec2 viewportSize = ENGINE::GETVIEWPORTSIZE();
    const float viewportWidth = viewportSize.x;
    const float availableWidth = std::max(
        0.0f,
        viewportWidth - kChatLeftMargin - kChatInputRightMargin -
            kChatInputHorizontalPadding * 2.0f
    );
    const std::string prefix = "> ";

    while (layout.visibleStart < caretIndex) {
        const std::string candidate =
            prefix + inputState_.text.substr(
                         layout.visibleStart,
                         caretIndex - layout.visibleStart
                     );
        if (inputLineElement_->measureText(candidate).x <= availableWidth) {
            break;
        }
        ++layout.visibleStart;
    }

    while (layout.visibleEnd > caretIndex) {
        const std::string candidate =
            prefix + inputState_.text.substr(
                         layout.visibleStart,
                         layout.visibleEnd - layout.visibleStart
                     );
        if (inputLineElement_->measureText(candidate).x <= availableWidth) {
            break;
        }
        --layout.visibleEnd;
    }

    layout.lineText =
        prefix + inputState_.text.substr(
                     layout.visibleStart,
                     layout.visibleEnd - layout.visibleStart
                 );
    const glm::vec2 lineSize = inputLineElement_->measureText(layout.lineText);
    const glm::vec2 linePosition = resolveHUDPosition(
        makeHUDLayout(HUDAnchor::BOTTOM_LEFT, glm::vec2(kChatLeftMargin, -kChatInputBottomMargin)),
        static_cast<int>(viewportSize.x),
        static_cast<int>(viewportSize.y),
        lineSize
    );
    layout.textX = static_cast<int>(std::round(linePosition.x));
    layout.textY = static_cast<int>(std::round(linePosition.y));
    layout.textHeight = lineSize.y;
    layout.caretPixelOffset = inputLineElement_->measureText(
        prefix + inputState_.text.substr(layout.visibleStart, caretIndex - layout.visibleStart)
    ).x;

    if (inputState_.hasSelection()) {
        const std::size_t visibleSelectionStart = std::max(
            inputState_.selectionFirst(),
            layout.visibleStart
        );
        const std::size_t visibleSelectionEnd = std::min(
            inputState_.selectionLast(),
            layout.visibleEnd
        );

        if (visibleSelectionStart < visibleSelectionEnd) {
            layout.selectionPixelStart = inputLineElement_->measureText(
                prefix + inputState_.text.substr(
                             layout.visibleStart,
                             visibleSelectionStart - layout.visibleStart
                         )
            ).x;
            layout.selectionPixelEnd = inputLineElement_->measureText(
                prefix + inputState_.text.substr(
                             layout.visibleStart,
                             visibleSelectionEnd - layout.visibleStart
                         )
            ).x;
            layout.showSelection =
                layout.selectionPixelEnd > layout.selectionPixelStart;
        }
    }

    layout.valid = true;
    return layout;
}

bool GameChat::tryGetInputSelectionRect(glm::ivec4& outRect) const {
    const ResolvedInputLayout layout = resolveInputLayout();
    if (!layout.valid || !layout.showSelection) {
        return false;
    }

    outRect = glm::ivec4(
        layout.textX + static_cast<int>(std::round(layout.selectionPixelStart)),
        layout.textY - static_cast<int>(kChatSelectionHeightPadding),
        static_cast<int>(std::round(layout.selectionPixelEnd - layout.selectionPixelStart)),
        static_cast<int>(std::round(layout.textHeight + kChatSelectionHeightPadding * 2.0f))
    );
    return outRect.z > 0 && outRect.w > 0;
}

bool GameChat::tryGetInputCaretRect(glm::ivec4& outRect) const {
    const ResolvedInputLayout layout = resolveInputLayout();
    if (!layout.valid || !layout.showCaret) {
        return false;
    }

    outRect = glm::ivec4(
        layout.textX + static_cast<int>(std::round(layout.caretPixelOffset)),
        layout.textY - static_cast<int>(kChatSelectionHeightPadding),
        2,
        static_cast<int>(std::round(layout.textHeight + kChatSelectionHeightPadding * 2.0f))
    );
    return outRect.w > 0;
}

bool GameChat::isMouseInsideInput(float mouseX, float mouseY) const {
    const glm::vec2 viewportSize = ENGINE::GETVIEWPORTSIZE();
    const glm::ivec4 inputRect = makeChatInputRect(
        static_cast<int>(viewportSize.x),
        static_cast<int>(viewportSize.y)
    );

    return mouseX >= static_cast<float>(inputRect.x) &&
           mouseY >= static_cast<float>(inputRect.y) &&
           mouseX <= static_cast<float>(inputRect.x + inputRect.z) &&
           mouseY <= static_cast<float>(inputRect.y + inputRect.w);
}

void GameChat::beginMouseSelection(float mouseX) {
    const ResolvedInputLayout layout = resolveInputLayout();
    if (!layout.valid || !inputLineElement_) {
        return;
    }

    const float prefixWidth = inputLineElement_->measureText("> ").x;
    const float localX = std::max(0.0f, mouseX - static_cast<float>(layout.textX) - prefixWidth);
    const std::size_t visibleCount = layout.visibleEnd - layout.visibleStart;
    std::size_t bestIndex = 0;
    float bestDistance = std::numeric_limits<float>::max();

    for (std::size_t index = 0; index <= visibleCount; ++index) {
        const float caretX = inputLineElement_->measureText(
            inputState_.text.substr(layout.visibleStart, index)
        ).x;
        const float distance = std::fabs(localX - caretX);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = index;
        }
    }

    inputState_.mouseSelectionAnchor = layout.visibleStart + bestIndex;
    inputState_.mouseSelecting = true;
    inputState_.moveCaretTo(inputState_.mouseSelectionAnchor);
}

void GameChat::updateMouseSelection(float mouseX) {
    const ResolvedInputLayout layout = resolveInputLayout();
    if (!layout.valid || !inputLineElement_) {
        return;
    }

    const float prefixWidth = inputLineElement_->measureText("> ").x;
    const float localX = std::max(0.0f, mouseX - static_cast<float>(layout.textX) - prefixWidth);
    const std::size_t visibleCount = layout.visibleEnd - layout.visibleStart;
    std::size_t bestIndex = 0;
    float bestDistance = std::numeric_limits<float>::max();

    for (std::size_t index = 0; index <= visibleCount; ++index) {
        const float caretX = inputLineElement_->measureText(
            inputState_.text.substr(layout.visibleStart, index)
        ).x;
        const float distance = std::fabs(localX - caretX);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = index;
        }
    }

    inputState_.moveCaretTo(
        layout.visibleStart + bestIndex,
        true,
        inputState_.mouseSelectionAnchor
    );
}

void GameChat::endMouseSelection() {
    inputState_.endMouseSelection();
}

#pragma endregion

#pragma region 5. Autocomplete & Parsing Helpers
// --- 5. Autocomplete & Parsing Helpers ---

void GameChat::autocompleteInput() {
    const std::vector<std::string> candidates = autocompleteCandidates();
    if (candidates.empty()) {
        return;
    }

    if (candidates.size() == 1) {
        inputState_.setText(candidates.front());
        return;
    }

    const std::string prefix = longestCommonPrefix(candidates);
    if (prefix.size() > inputState_.text.size()) {
        inputState_.setText(prefix);
    }
}

std::vector<std::string> GameChat::autocompleteCandidates() const {
    if (inputState_.text.empty() || inputState_.text.front() != '/') {
        return {};
    }

    const std::string normalized = lowercase(inputState_.text);

    auto filterByPrefix = [&normalized](const std::vector<std::string>& options) {
        std::vector<std::string> candidates;
        for (const std::string& option : options) {
            if (option.rfind(normalized, 0) == 0) {
                candidates.push_back(option);
            }
        }
        return candidates;
        };

    if (normalized == "/debug wireframe" || normalized.rfind("/debug wireframe ", 0) == 0) {
        return filterByPrefix({ "/debug wireframe true", "/debug wireframe false" });
    }

    if (normalized == "/debug camera:culling" || normalized.rfind("/debug camera:culling ", 0) == 0) {
        return filterByPrefix({ "/debug camera:culling true", "/debug camera:culling false" });
    }

    if (normalized == "/debug world:mesh" || normalized.rfind("/debug world:mesh ", 0) == 0) {
        return filterByPrefix({ "/debug world:mesh naive", "/debug world:mesh greedy" });
    }

    if (normalized == "/summon entity:guy" || normalized.rfind("/summon entity:guy ", 0) == 0) {
        return filterByPrefix({ "/summon entity:guy normal", "/summon entity:guy lookat" });
    }

    if (normalized == "/sandbox" || normalized.rfind("/sandbox ", 0) == 0) {
        return filterByPrefix({ "/sandbox ", "/sandbox true", "/sandbox false" });
    }

    if (normalized == "/spawnpoint" || normalized.rfind("/spawnpoint ", 0) == 0) {
        return filterByPrefix({ "/spawnpoint" });
    }

    return filterByPrefix({
        "/help",
        "/clear",
        "/get ",
        "/set level ",
        "/set xp ",
        "/reset level",
        "/reset xp",
        "/sandbox ",
        "/seed",
        "/worldseed",
        "/health",
        "/life",
        "/spawnpoint",
        "/summon entity:guy ",
        "/debug wireframe ",
        "/debug camera:culling ",
        "/debug world:mesh ",
        });
}

std::vector<std::string> GameChat::visibleHistoryLines() const {
    const std::vector<HistoryLine> richLines = visibleHistoryRichLines();
    std::vector<std::string> lines;
    lines.reserve(richLines.size());

    for (const HistoryLine& line : richLines) {
        lines.push_back(plainTextForLine(line));
    }

    return lines;
}

std::vector<GameChat::HistoryLine> GameChat::visibleHistoryRichLines() const {
    const std::vector<HistoryLine> lines = allHistoryRichLines();
    if (lines.empty()) {
        return {};
    }

    const std::size_t visibleLineCount = std::min(
        lines.size(),
        static_cast<std::size_t>(kVisibleHistoryLines)
    );
    const int maxScrollOffset = static_cast<int>(
        lines.size() - visibleLineCount
    );
    const int scrollOffset = open_
        ? std::clamp(historyScrollOffset_, 0, maxScrollOffset)
        : 0;
    const std::size_t startIndex = static_cast<std::size_t>(maxScrollOffset - scrollOffset);

    std::vector<HistoryLine> visibleLines;
    visibleLines.reserve(visibleLineCount);
    for (std::size_t index = 0; index < visibleLineCount; index++) {
        visibleLines.push_back(lines[startIndex + index]);
    }

    return visibleLines;
}

std::string GameChat::historyLineSegmentText(int lineIndex, int segmentIndex) const {
    if (lineIndex < 0 || lineIndex >= kVisibleHistoryLines ||
        segmentIndex < 0 || segmentIndex >= kMaxHistorySegmentsPerLine) {
        return {};
    }

    const std::vector<HistoryLine> lines = visibleHistoryRichLines();
    if (lineIndex >= static_cast<int>(lines.size())) {
        return {};
    }

    const HistoryLine& line = lines[static_cast<std::size_t>(lineIndex)];
    if (segmentIndex >= static_cast<int>(line.segments.size())) {
        return {};
    }

    return line.segments[static_cast<std::size_t>(segmentIndex)].text;
}

glm::vec3 GameChat::historyLineSegmentColor(int lineIndex, int segmentIndex) const {
    if (lineIndex < 0 || lineIndex >= kVisibleHistoryLines ||
        segmentIndex < 0 || segmentIndex >= kMaxHistorySegmentsPerLine) {
        return kChatDefaultHistoryColor;
    }

    const std::vector<HistoryLine> lines = visibleHistoryRichLines();
    if (lineIndex >= static_cast<int>(lines.size())) {
        return kChatDefaultHistoryColor;
    }

    const HistoryLine& line = lines[static_cast<std::size_t>(lineIndex)];
    if (segmentIndex >= static_cast<int>(line.segments.size())) {
        return kChatDefaultHistoryColor;
    }

    return line.segments[static_cast<std::size_t>(segmentIndex)].color;
}

float GameChat::historyLineSegmentOffset(int lineIndex, int segmentIndex,
                                         const hudWatchText& textElement) const {
    if (lineIndex < 0 || lineIndex >= kVisibleHistoryLines ||
        segmentIndex <= 0 || segmentIndex >= kMaxHistorySegmentsPerLine) {
        return 0.0f;
    }

    const std::vector<HistoryLine> lines = visibleHistoryRichLines();
    if (lineIndex >= static_cast<int>(lines.size())) {
        return 0.0f;
    }

    const HistoryLine& line = lines[static_cast<std::size_t>(lineIndex)];
    const int clampedSegmentIndex =
        std::min(segmentIndex, static_cast<int>(line.segments.size()));

    float offset = 0.0f;
    for (int index = 0; index < clampedSegmentIndex; index++) {
        offset += textElement.measureText(line.segments[static_cast<std::size_t>(index)].text).x;
    }

    return offset;
}

std::vector<std::string> GameChat::wrapChatText(const std::string& text,
                                                std::size_t maxCharactersPerLine) {
    if (text.empty() || maxCharactersPerLine == 0) {
        return {};
    }

    const std::vector<HistoryLine> richLines =
        wrapChatSegments({ { text, kChatDefaultHistoryColor } }, maxCharactersPerLine);

    std::vector<std::string> wrappedLines;
    wrappedLines.reserve(richLines.size());

    for (const HistoryLine& line : richLines) {
        wrappedLines.push_back(plainTextForLine(line));
    }

    return wrappedLines;
}

std::vector<GameChat::HistoryLine> GameChat::wrapChatSegments(
        const std::vector<GameChatTextSegment>& segments,
        std::size_t maxCharactersPerLine) {
    if (segments.empty() || maxCharactersPerLine == 0) {
        return {};
    }

    std::vector<HistoryLine> wrappedLines;
    HistoryLine currentLine;
    std::size_t currentLength = 0;
    GameChatTextSegment pendingSpace{ " ", kChatDefaultHistoryColor };
    bool hasPendingSpace = false;

    auto flushCurrentLine = [&]() {
        wrappedLines.push_back(currentLine);
        currentLine = HistoryLine{};
        currentLength = 0;
        hasPendingSpace = false;
    };

    auto appendText = [&](const std::string& text, const glm::vec3& color) {
        appendHistorySegment(currentLine.segments, { text, color });
        currentLength += text.size();
    };

    auto appendWord = [&](const std::string& word, const glm::vec3& color) {
        std::size_t wordOffset = 0;

        while (wordOffset < word.size()) {
            const bool needsSpace = hasPendingSpace && currentLength > 0;
            const std::size_t spaceLength = needsSpace ? pendingSpace.text.size() : 0;
            const std::size_t availableLength =
                currentLength + spaceLength < maxCharactersPerLine
                    ? maxCharactersPerLine - currentLength - spaceLength
                    : 0;

            if (currentLength > 0 && availableLength == 0) {
                flushCurrentLine();
                continue;
            }

            if (needsSpace) {
                appendText(pendingSpace.text, pendingSpace.color);
            }
            hasPendingSpace = false;

            const std::size_t remainingLength = word.size() - wordOffset;
            const std::size_t lineCapacity =
                currentLength < maxCharactersPerLine
                    ? maxCharactersPerLine - currentLength
                    : maxCharactersPerLine;
            const std::size_t chunkLength = std::min(remainingLength, lineCapacity);

            appendText(word.substr(wordOffset, chunkLength), color);
            wordOffset += chunkLength;

            if (wordOffset < word.size()) {
                flushCurrentLine();
            }
        }
    };

    for (const GameChatTextSegment& segment : segments) {
        std::size_t offset = 0;

        while (offset < segment.text.size()) {
            const unsigned char ch = static_cast<unsigned char>(segment.text[offset]);

            if (segment.text[offset] == '\r') {
                offset++;
                continue;
            }

            if (segment.text[offset] == '\n') {
                flushCurrentLine();
                offset++;
                continue;
            }

            if (std::isspace(ch) != 0) {
                if (currentLength > 0) {
                    pendingSpace = { " ", segment.color };
                    hasPendingSpace = true;
                }
                offset++;
                continue;
            }

            std::size_t wordEnd = offset + 1;
            while (wordEnd < segment.text.size()) {
                const unsigned char wordCh = static_cast<unsigned char>(segment.text[wordEnd]);
                if (std::isspace(wordCh) != 0) {
                    break;
                }
                wordEnd++;
            }

            appendWord(segment.text.substr(offset, wordEnd - offset), segment.color);
            offset = wordEnd;
        }
    }

    if (!currentLine.segments.empty()) {
        wrappedLines.push_back(currentLine);
    }

    return wrappedLines;
}

std::string GameChat::plainTextForLine(const HistoryLine& line) {
    std::string text;

    for (const GameChatTextSegment& segment : line.segments) {
        text += segment.text;
    }

    return text;
}

std::string GameChat::trim(const std::string& value) {
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        start++;
    }

    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        end--;
    }

    return value.substr(start, end - start);
}

std::string GameChat::lowercase(std::string value) {
    return lowercaseAscii(std::move(value));
}

std::string GameChat::longestCommonPrefix(const std::vector<std::string>& values) {
    if (values.empty()) {
        return {};
    }

    std::string prefix = values.front();
    for (std::size_t index = 1; index < values.size() && !prefix.empty(); index++) {
        const std::string& value = values[index];
        std::size_t sharedLength = 0;

        while (sharedLength < prefix.size() && sharedLength < value.size() &&
            prefix[sharedLength] == value[sharedLength]) {
            sharedLength++;
        }

        prefix.resize(sharedLength);
    }

    return prefix;
}

bool GameChat::tryParsePositiveAmount(const std::string& value, int& outAmount) {
    if (value.empty()) {
        return false;
    }

    char* parseEnd = nullptr;
    const long parsed = std::strtol(value.c_str(), &parseEnd, 10);

    if (parseEnd == value.c_str() || *parseEnd != '\0' || parsed <= 0 ||
        parsed > static_cast<long>(INT_MAX)) {
        return false;
    }

    outAmount = static_cast<int>(parsed);
    return true;
}

bool GameChat::tryParseNonNegativeInteger(const std::string& value, long long& outAmount) {
    if (value.empty()) {
        return false;
    }

    char* parseEnd = nullptr;
    const long long parsed = std::strtoll(value.c_str(), &parseEnd, 10);

    if (parseEnd == value.c_str() || *parseEnd != '\0' || parsed < 0) {
        return false;
    }

    outAmount = parsed;
    return true;
}

bool GameChat::tryParseNonNegativeNumber(const std::string& value, float& outAmount) {
    if (value.empty()) {
        return false;
    }

    char* parseEnd = nullptr;
    const double parsed = std::strtod(value.c_str(), &parseEnd);

    if (parseEnd == value.c_str() || *parseEnd != '\0' || parsed < 0.0 ||
        !std::isfinite(parsed)) {
        return false;
    }

    outAmount = static_cast<float>(parsed);
    return std::isfinite(outAmount);
}

bool GameChat::tryParseBooleanWord(const std::string& value, bool& outValue) {
    const std::string normalized = lowercase(value);

    if (normalized == "true" || normalized == "on" || normalized == "1") {
        outValue = true;
        return true;
    }

    if (normalized == "false" || normalized == "off" || normalized == "0") {
        outValue = false;
        return true;
    }

    return false;
}

#pragma endregion

#pragma region 6. Command Execution Routing
// --- 6. Command Execution Routing ---

void GameChat::submit(GameChatCommandContext& commandContext) {
    const std::string submitted = trim(inputState_.text);
    close();

    if (submitted.empty()) {
        return;
    }

    lastSubmittedInput_ = submitted;

    if (submitted.front() == '/') {
        if (!executeCommand(commandContext, submitted.substr(1))) {
            pushHistory("[Debug] Unknown command. Use /help.");
        }
        return;
    }

    pushHistory(std::string("You: ") + submitted);
}

bool GameChat::executeCommand(GameChatCommandContext& commandContext, const std::string& commandLine) {
    const std::string trimmed = trim(commandLine);
    if (trimmed.empty()) {
        pushHistory("[Debug] Empty command.");
        return true;
    }

    const std::vector<std::string> tokens = splitWhitespace(trimmed);
    if (tokens.empty()) {
        pushHistory("[Debug] Empty command.");
        return true;
    }

    const std::string commandName = lowercase(tokens.front());

    if (commandName == "get") {
        const std::size_t argumentsOffset = trimmed.find_first_not_of(" \t", tokens.front().size());
        const std::string arguments = argumentsOffset == std::string::npos ? std::string{} : trimmed.substr(argumentsOffset);
        return executeGetCommand(commandContext, arguments);
    }

    if (commandName == "clear") {
        const std::size_t argumentsOffset = trimmed.find_first_not_of(" \t", tokens.front().size());
        const std::string arguments = argumentsOffset == std::string::npos ? std::string{} : trimmed.substr(argumentsOffset);
        return executeClearCommand(commandContext, arguments);
    }

    if (commandName == "set") {
        const std::size_t argumentsOffset = trimmed.find_first_not_of(" \t", tokens.front().size());
        const std::string arguments = argumentsOffset == std::string::npos ? std::string{} : trimmed.substr(argumentsOffset);
        return executeSetCommand(commandContext, arguments);
    }

    if (commandName == "reset") {
        const std::size_t argumentsOffset = trimmed.find_first_not_of(" \t", tokens.front().size());
        const std::string arguments = argumentsOffset == std::string::npos ? std::string{} : trimmed.substr(argumentsOffset);
        return executeResetCommand(commandContext, arguments);
    }

    if (commandName == "debug") {
        const std::size_t argumentsOffset = trimmed.find_first_not_of(" \t", tokens.front().size());
        const std::string arguments = argumentsOffset == std::string::npos ? std::string{} : trimmed.substr(argumentsOffset);
        return executeDebugCommand(commandContext, arguments);
    }

    if (commandName == "summon") {
        const std::size_t argumentsOffset = trimmed.find_first_not_of(" \t", tokens.front().size());
        const std::string arguments = argumentsOffset == std::string::npos ? std::string{} : trimmed.substr(argumentsOffset);
        return executeSummonCommand(commandContext, arguments);
    }

    if (commandName == "sandbox") {
        const std::size_t argumentsOffset = trimmed.find_first_not_of(" \t", tokens.front().size());
        const std::string arguments = argumentsOffset == std::string::npos ? std::string{} : trimmed.substr(argumentsOffset);
        return executeSandboxCommand(commandContext, arguments);
    }

    if (commandName == "spawnpoint") {
        if (tokens.size() != 1) {
            pushHistory("[Debug] Usage: /spawnpoint");
            return true;
        }

        FractalWorld* world = commandContext.worldStack.currentWorld();
        if (!world) {
            pushHistory("[Debug] There is no active world.");
            return true;
        }

        commandContext.player.setSpawnpoint(commandContext.player.camera.position,
                                            world->seed, world->biomeSelection,
                                            commandContext.worldStack.snapshotTraversalStack());

        char buffer[256];
        std::snprintf(buffer, sizeof(buffer),
                      "[Debug] Spawnpoint set to %.2f, %.2f, %.2f in %s.",
                      commandContext.player.camera.position.x,
                      commandContext.player.camera.position.y,
                      commandContext.player.camera.position.z,
                      world->biomeSelection.displayName.c_str());
        std::printf("%s\n", buffer);
        pushHistory(buffer);
        return true;
    }

    if (commandName == "health" || commandName == "life") {
        if (tokens.size() != 2) {
            pushHistory("[Debug] Usage: /health <1-20>");
            return true;
        }

        int amount = 0;
        if (!tryParsePositiveAmount(tokens[1], amount) || amount > 20) {
            pushHistory("[Debug] Usage: /health <1-20>");
            return true;
        }

        commandContext.player.setLifePoints(amount);

        std::printf("[Debug] Player health set to %d.\n", amount);
        pushHistory(std::string("[Debug] Player health set to ") + std::to_string(amount) + ".");

        return true;
    }

    if (commandName == "seed" || commandName == "worldseed") {
        if (tokens.size() != 1) {
            pushHistory("[Debug] Usage: /seed");
            return true;
        }

        FractalWorld* world = commandContext.worldStack.currentWorld();
        if (!world) {
            pushHistory("[Debug] There is no active world.");
            return true;
        }

        char buffer[128];
        std::snprintf(buffer, sizeof(buffer), "[Debug] World Seed: %u", world->seed);

        std::printf("%s\n", buffer);
        pushHistory(buffer);
        return true;
    }

    if (commandName == "help") {
        pushHistory("[Debug] /get <item> [amount]");
        pushHistory("[Debug] /clear");
        pushHistory("[Debug] /set level <num>");
        pushHistory("[Debug] /set xp <num>");
        pushHistory("[Debug] /reset level");
        pushHistory("[Debug] /reset xp");
        pushHistory("[Debug] /sandbox [true|false]");
        pushHistory("[Debug] /spawnpoint");
        pushHistory("[Debug] /seed or /worldseed");
        pushHistory("[Debug] /summon entity:guy <normal|lookat>");
        pushHistory("[Debug] /health or /life <1-20>");
        pushHistory("[Debug] /debug wireframe <true|false>");
        pushHistory("[Debug] /debug camera:culling <true|false>");
        pushHistory("[Debug] /debug world:mesh <naive|greedy>");
        return true;
    }

    return false;
}

#pragma endregion

#pragma region 7. Command Implementations
// --- 7. Command Implementations ---

bool GameChat::executeGetCommand(GameChatCommandContext& commandContext, const std::string& arguments) {
    const std::vector<std::string> tokens = splitWhitespace(arguments);
    if (tokens.empty()) {
        pushHistory("[Debug] Usage: /get <item> [amount]");
        return true;
    }

    InventoryItem item{};
    const std::string itemToken = stripInventoryPrefix(tokens[0]);
    if (!tryParseInventoryItem(itemToken, item) || item.empty()) {
        pushHistory("[Debug] Unknown item. Example: /get stone 6");
        return true;
    }

    int amount = 1;
    if (tokens.size() >= 2 && !tryParsePositiveAmount(tokens[1], amount)) {
        pushHistory("[Debug] Amount must be a positive integer.");
        return true;
    }

    if (!commandContext.player.tryAddItemToInventory(item, amount)) {
        pushHistory("[Debug] Inventory has no room for that request.");
        return true;
    }

    char buffer[160];
    std::snprintf(
        buffer, sizeof(buffer), "[Debug] Added %d x %s.",
        amount, getInventoryItemDisplayName(item)
    );
    pushHistory(buffer);

    return true;
}

bool GameChat::executeClearCommand(GameChatCommandContext& commandContext, const std::string& arguments) {
    const std::vector<std::string> tokens = splitWhitespace(arguments);
    if (!tokens.empty()) {
        pushHistory("[Debug] Usage: /clear");
        return true;
    }

    const bool inventoryWasOpen = commandContext.player.isInventoryOpen();
    commandContext.player.clearHotbar();
    if (inventoryWasOpen) {
        commandContext.player.setInventoryOpen(true);
    }

    std::printf("[Debug] Player inventory cleared.\n");
    pushHistory("[Debug] Player inventory cleared.");
    return true;
}

bool GameChat::executeSetCommand(GameChatCommandContext& commandContext, const std::string& arguments) {
    const std::vector<std::string> tokens = splitWhitespace(arguments);
    if (tokens.size() < 2) {
        pushHistory("[Debug] Usage: /set level <num>");
        pushHistory("[Debug] Usage: /set xp <num>");
        return true;
    }

    const std::string target = lowercase(tokens[0]);

    if (target == "level") {
        if (tokens.size() != 2) {
            pushHistory("[Debug] Usage: /set level <num>");
            return true;
        }

        long long requestedLevel = 0;
        if (!tryParseNonNegativeInteger(tokens[1], requestedLevel)) {
            pushHistory("[Debug] Usage: /set level <num>");
            return true;
        }

        const long long clampedLevelLong = std::min(
            requestedLevel,
            static_cast<long long>(std::numeric_limits<int>::max())
        );
        const int clampedLevel = static_cast<int>(clampedLevelLong);

        commandContext.player.setExperienceLevel(clampedLevel);

        char buffer[160];
        std::snprintf(buffer, sizeof(buffer), "[Debug] Player level set to %d.", clampedLevel);
        std::printf("%s\n", buffer);
        pushHistory(buffer);
        return true;
    }

    if (target == "xp") {
        if (tokens.size() != 2) {
            pushHistory("[Debug] Usage: /set xp <num>");
            return true;
        }

        float requestedExperiencePoints = 0.0f;
        if (!tryParseNonNegativeNumber(tokens[1], requestedExperiencePoints)) {
            pushHistory("[Debug] Usage: /set xp <num>");
            return true;
        }

        const float maxExperiencePoints = commandContext.player.getMaxExperiencePoints();
        const float carriedLevelsFloat = maxExperiencePoints > 0.0f
            ? std::floor(requestedExperiencePoints / maxExperiencePoints)
            : 0.0f;
        const long long carriedLevels = carriedLevelsFloat > static_cast<float>(std::numeric_limits<long long>::max())
            ? std::numeric_limits<long long>::max()
            : static_cast<long long>(carriedLevelsFloat);

        const long long requestedLevelLong =
            static_cast<long long>(commandContext.player.getExperienceLevel()) + carriedLevels;
        const int targetLevel = static_cast<int>(std::min(
            requestedLevelLong,
            static_cast<long long>(std::numeric_limits<int>::max())
        ));

        if (targetLevel != commandContext.player.getExperienceLevel()) {
            commandContext.player.setExperienceLevel(targetLevel);
        }

        const float remainingExperiencePoints = maxExperiencePoints > 0.0f
            ? std::fmod(requestedExperiencePoints, maxExperiencePoints)
            : requestedExperiencePoints;
        commandContext.player.setExperiencePoints(remainingExperiencePoints);

        char buffer[192];
        std::snprintf(
            buffer,
            sizeof(buffer),
            "[Debug] Player XP set to %.2f. Level is now %d.",
            requestedExperiencePoints,
            commandContext.player.getExperienceLevel()
        );
        std::printf("%s\n", buffer);
        pushHistory(buffer);
        return true;
    }

    pushHistory("[Debug] Usage: /set level <num>");
    pushHistory("[Debug] Usage: /set xp <num>");
    return true;
}

bool GameChat::executeResetCommand(GameChatCommandContext& commandContext, const std::string& arguments) {
    const std::vector<std::string> tokens = splitWhitespace(arguments);
    if (tokens.size() != 1) {
        pushHistory("[Debug] Usage: /reset level");
        pushHistory("[Debug] Usage: /reset xp");
        return true;
    }

    const std::string target = lowercase(tokens[0]);

    if (target == "level") {
        commandContext.player.setExperienceLevel(0);

        std::printf("[Debug] Player level reset to 0.\n");
        pushHistory("[Debug] Player level reset to 0.");
        return true;
    }

    if (target == "xp") {
        commandContext.player.setExperiencePoints(0.0f);

        std::printf("[Debug] Player XP reset to 0.\n");
        pushHistory("[Debug] Player XP reset to 0.");
        return true;
    }

    pushHistory("[Debug] Usage: /reset level");
    pushHistory("[Debug] Usage: /reset xp");
    return true;
}

bool GameChat::executeDebugCommand(GameChatCommandContext& commandContext, const std::string& arguments) {
    const std::vector<std::string> tokens = splitWhitespace(arguments);
    if (tokens.size() < 2) {
        pushHistory("[Debug] Usage: /debug wireframe <true|false>");
        pushHistory("[Debug] Usage: /debug camera:culling <true|false>");
        pushHistory("[Debug] Usage: /debug world:mesh <naive|greedy>");
        return true;
    }

    const std::string debugTarget = lowercase(tokens[0]);
    const std::string debugValue = lowercase(tokens[1]);

    if (debugTarget == "wireframe") {
        bool enabled = false;
        if (!tryParseBooleanWord(debugValue, enabled)) {
            pushHistory("[Debug] Usage: /debug wireframe <true|false>");
            return true;
        }

        commandContext.wireframeMode = enabled;
        std::printf("[Debug] Wireframe mode: %s\n", enabled ? "ON" : "OFF");
        pushHistory(std::string("[Debug] Wireframe mode set to ") + (enabled ? "true." : "false."));
        return true;
    }

    if (debugTarget == "camera:culling") {
        bool enabled = false;
        if (!tryParseBooleanWord(debugValue, enabled)) {
            pushHistory("[Debug] Usage: /debug camera:culling <true|false>");
            return true;
        }

        commandContext.debugThirdPersonView = enabled;
        std::printf("[Debug] Third-person culling inspector: %s\n", enabled ? "ON" : "OFF");
        pushHistory(std::string("[Debug] Camera culling inspector set to ") + (enabled ? "true." : "false."));
        return true;
    }

    if (debugTarget == "world:mesh") {
        const bool greedyMeshing = debugValue == "greedy";
        if (!greedyMeshing && debugValue != "naive") {
            pushHistory("[Debug] Usage: /debug world:mesh <naive|greedy>");
            return true;
        }

        FractalWorld::setGreedyMeshingEnabled(greedyMeshing);
        commandContext.worldStack.markAllWorldMeshesDirty();
        std::printf("[Debug] World meshing: %s\n", greedyMeshing ? "GREEDY" : "NAIVE");
        pushHistory(std::string("[Debug] World meshing set to ") + (greedyMeshing ? "greedy." : "naive."));
        return true;
    }

    pushHistory("[Debug] Unknown debug target.");
    return true;
}

bool GameChat::executeSummonCommand(GameChatCommandContext& commandContext, const std::string& arguments) {
    const std::vector<std::string> tokens = splitWhitespace(arguments);
    if (tokens.empty()) {
        pushHistory("[Debug] Usage: /summon entity:guy <normal|lookat>");
        return true;
    }

    const std::string summonTarget = lowercase(tokens[0]);
    if (summonTarget != "entity:guy") {
        pushHistory("[Debug] Unknown summon target. Try entity:guy.");
        return true;
    }

    const std::string summonMode = tokens.size() >= 2 ? lowercase(tokens[1]) : "normal";
    std::string failureReason;

    if (summonMode == "normal") {
        FractalWorld* world = commandContext.worldStack.currentWorld();
        if (!world) {
            pushHistory("[Debug] Summon failed: there is no active world.");
            return true;
        }

        if (!tryForceSpawnWorldEnemyNearPlayer(*world, commandContext.player, EnemyType::Guy, &failureReason)) {
            pushHistory(std::string("[Debug] Summon failed: ") + failureReason);
            return true;
        }

        pushHistory("[Debug] Summoned entity:guy using normal spawn rules.");
        return true;
    }

    if (summonMode == "lookat") {
        if (!trySummonEnemyAtLookTarget(commandContext, EnemyType::Guy, failureReason)) {
            pushHistory(std::string("[Debug] Summon failed: ") + failureReason);
            return true;
        }

        pushHistory("[Debug] Summoned entity:guy at the current look target.");
        return true;
    }

    pushHistory("[Debug] Usage: /summon entity:guy <normal|lookat>");
    return true;
}

bool GameChat::executeSandboxCommand(GameChatCommandContext& commandContext, const std::string& arguments) {
    const std::vector<std::string> tokens = splitWhitespace(arguments);
    if (tokens.empty()) {
        commandContext.player.setSandboxModeEnabled(true);
        pushHistory("[Debug] Sandbox enabled. New universes can now be created without cooldown.");
        return true;
    }

    bool enabled = false;
    if (!tryParseBooleanWord(tokens[0], enabled)) {
        pushHistory("[Debug] Usage: /sandbox [true|false]");
        return true;
    }

    commandContext.player.setSandboxModeEnabled(enabled);
    if (enabled) {
        pushHistory("[Debug] Sandbox enabled. New universes can now be created without cooldown.");
    }
    else {
        pushHistory("[Debug] Sandbox disabled. New universes are limited to one every 10 minutes.");
    }
    return true;
}

#pragma endregion
