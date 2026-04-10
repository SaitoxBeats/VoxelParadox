// game_chat.cpp
// Unity mental model: HUD and Controller for the in-game chat.
// Handles opening/closing, capturing text input, autocomplete suggestions, and command execution.

#pragma region Includes

// 1. Standard Library
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

// 2. Third-party Libraries
#include <GLFW/glfw3.h>

// 3. Internal Engine/Core Modules
#include "engine/engine.hpp"
#include "engine/input.hpp"
#include "input/input_action_ids.hpp"
#include "input/input_action_system.hpp"

// 4. Local Project Modules
#include "runtime/game_chat.hpp"
#include "enemies/enemy_spawn_system.hpp"
#include "items/item_catalog.hpp"
#include "player/player.hpp"
#include "render/hud/hud_chat_background.hpp"
#include "render/hud/hud.hpp"
#include "render/hud/hud_text.hpp"
#include "render/hud/hud_watch_text.hpp"
#include "runtime/runtime_hud_ids.hpp"
#include "world/world_stack.hpp"

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
    constexpr float kChatInputBottomMargin = 30.0f;
    constexpr float kChatHistoryBottomMargin = 64.0f;
    constexpr float kChatLineSpacing = 24.0f;
    constexpr float kChatSuggestionLineSpacing = 22.0f;
    constexpr float kChatSuggestionBottomMargin = 60.0f;
    constexpr std::size_t kChatMaxCharactersPerLine = 46;
    constexpr float kChatInputRightMargin = 12.0f;
    constexpr float kChatInputHorizontalPadding = 12.0f;

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

        const BlockType supportType = world->getBlock(targetBlock);
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
    inputState_.resetRepeats();
    Input::enableTextInput(true);
}

void GameChat::close() {
    if (!open_) {
        return;
    }

    open_ = false;
    inputState_.setText({});
    inputState_.resetRepeats();
    Input::enableTextInput(false);
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
    inputState_.insertText(Input::consumeTypedChars());

    if (Input::keyDown(GLFW_KEY_LEFT_CONTROL) && Input::keyPressed(GLFW_KEY_A)) {
        inputState_.selectAll();
        return true;
    }

    if (TextInputState::consumeHeldKey(GLFW_KEY_LEFT, now, inputState_.nextLeftRepeatTime)) {
        inputState_.moveCaretLeft();
    }

    if (TextInputState::consumeHeldKey(GLFW_KEY_RIGHT, now, inputState_.nextRightRepeatTime)) {
        inputState_.moveCaretRight();
    }

    if (TextInputState::consumeHeldKey(GLFW_KEY_BACKSPACE, now, inputState_.nextBackspaceRepeatTime)) {
        inputState_.eraseBackward();
    }

    if (TextInputState::consumeHeldKey(GLFW_KEY_DELETE, now, inputState_.nextDeleteRepeatTime)) {
        inputState_.eraseForward();
    }

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

        auto* line = addGroupedHudElement(
            HUD::watchText(
                [this, lineIndex](std::string& out) {
                    out = historyLineText(lineIndex);
                },
                makeHUDLayout(HUDAnchor::BOTTOM_LEFT, glm::vec2(kChatLeftMargin, yOffset)),
                glm::vec2(1.0f), 20
            ),
            kChatHistoryGroup, lineIndex
        );

        if (line) {
            line->setColor(glm::vec3(0.92f, 0.95f, 1.0f));
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

    const std::size_t caretIndex = std::min(inputState_.caretIndex, inputState_.text.size());
    const int blinkPhase = static_cast<int>(ENGINE::GETTIME() * 2.0);
    const bool showCaret = (blinkPhase % 2) == 0;

    if (!inputLineElement_) {
        std::string fallbackLine = "> ";
        fallbackLine += inputState_.text.substr(0, caretIndex);
        if (showCaret) {
            fallbackLine += '_';
        }
        fallbackLine += inputState_.text.substr(caretIndex);
        return fallbackLine;
    }

    const float viewportWidth = ENGINE::GETVIEWPORTSIZE().x;
    const float availableWidth = std::max(
        0.0f,
        viewportWidth - kChatLeftMargin - kChatInputRightMargin - kChatInputHorizontalPadding * 2.0f
    );

    const std::string prefix = "> ";
    std::size_t visibleStart = 0;
    std::size_t visibleEnd = inputState_.text.size();

    while (visibleStart < caretIndex) {
        const std::string candidate =
            prefix + inputState_.text.substr(visibleStart, caretIndex - visibleStart);
        if (inputLineElement_->measureText(candidate).x <= availableWidth) {
            break;
        }
        ++visibleStart;
    }

    while (visibleEnd > caretIndex) {
        std::string candidate = prefix + inputState_.text.substr(visibleStart, visibleEnd - visibleStart);
        if (showCaret) {
            candidate.insert(prefix.size() + (caretIndex - visibleStart), 1, '_');
        }

        if (inputLineElement_->measureText(candidate).x <= availableWidth) {
            break;
        }
        --visibleEnd;
    }

    std::string line = prefix;
    line += inputState_.text.substr(visibleStart, caretIndex - visibleStart);

    if (showCaret) {
        line += '_';
    }

    line += inputState_.text.substr(caretIndex, visibleEnd - caretIndex);

    return line;
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
    return static_cast<int>(visibleHistoryLines().size());
}

int GameChat::visibleSuggestionLineCount() const {
    return static_cast<int>(autocompleteCandidates().size());
}

void GameChat::pushHistory(const std::string& text) {
    if (text.empty()) {
        return;
    }

    history_.push_back({ text, ENGINE::GETTIME() });
    while (history_.size() > static_cast<std::size_t>(kMaxHistoryEntries)) {
        history_.pop_front();
    }
}

bool GameChat::shouldShowHistory() const {
    return !visibleHistoryLines().empty();
}

bool GameChat::shouldShowSuggestions() const {
    return open_ && !autocompleteCandidates().empty();
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
        "/get ",
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
    std::vector<const Entry*> visibleEntries;
    visibleEntries.reserve(kVisibleHistoryLines);

    const double now = ENGINE::GETTIME();
    for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
        if (!open_ && (now - it->timestampSeconds) > kClosedHistoryLifetimeSeconds) {
            continue;
        }

        visibleEntries.push_back(&(*it));
        if (visibleEntries.size() >= static_cast<std::size_t>(kVisibleHistoryLines)) {
            break;
        }
    }

    std::reverse(visibleEntries.begin(), visibleEntries.end());

    std::vector<std::string> lines;
    lines.reserve(kVisibleHistoryLines);

    for (const Entry* entry : visibleEntries) {
        if (!entry) {
            continue;
        }

        const std::vector<std::string> wrappedLines =
            wrapChatText(entry->text, kChatMaxCharactersPerLine);
        lines.insert(lines.end(), wrappedLines.begin(), wrappedLines.end());
    }

    if (lines.size() > static_cast<std::size_t>(kVisibleHistoryLines)) {
        lines.erase(
            lines.begin(),
            lines.begin() + static_cast<std::size_t>(lines.size() - kVisibleHistoryLines)
        );
    }

    return lines;
}

std::vector<std::string> GameChat::wrapChatText(const std::string& text,
                                                std::size_t maxCharactersPerLine) {
    if (text.empty() || maxCharactersPerLine == 0) {
        return {};
    }

    std::vector<std::string> wrappedLines;
    std::istringstream lineStream(text);
    std::string sourceLine;

    while (std::getline(lineStream, sourceLine)) {
        if (sourceLine.empty()) {
            wrappedLines.push_back({});
            continue;
        }

        std::istringstream wordStream(sourceLine);
        std::string currentLine;
        std::string word;

        while (wordStream >> word) {
            std::size_t wordOffset = 0;
            while (wordOffset < word.size()) {
                const std::size_t remainingWordLength = word.size() - wordOffset;
                const std::size_t chunkLength =
                    std::min(maxCharactersPerLine, remainingWordLength);
                const std::string chunk = word.substr(wordOffset, chunkLength);

                if (currentLine.empty()) {
                    currentLine = chunk;
                } else if (currentLine.size() + 1 + chunk.size() <= maxCharactersPerLine) {
                    currentLine += " " + chunk;
                } else {
                    wrappedLines.push_back(currentLine);
                    currentLine = chunk;
                }

                wordOffset += chunkLength;
                if (wordOffset < word.size()) {
                    wrappedLines.push_back(currentLine);
                    currentLine.clear();
                }
            }
        }

        if (!currentLine.empty()) {
            wrappedLines.push_back(currentLine);
        }
    }

    if (wrappedLines.empty()) {
        wrappedLines.push_back(text);
    }

    return wrappedLines;
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
