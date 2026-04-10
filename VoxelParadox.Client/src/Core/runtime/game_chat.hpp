#pragma once

#include <deque>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/support/text_input.hpp"

class Player;
class WorldStack;
class hudWatchText;

struct GameChatCommandContext {
  Player& player;
  WorldStack& worldStack;
  bool& wireframeMode;
  bool& debugThirdPersonView;
};

class GameChat {
public:
  bool isOpen() const {
    return open_;
  }

  void open();
  void close();

  bool handleFrameInput(GameChatCommandContext& commandContext,
                        bool allowOpenChat);

  void setupHud();
  void syncHudState() const;

  std::string historyLineText(int lineIndex) const;
  std::string inputLineText() const;
  std::string suggestionLineText(int lineIndex) const;
  int visibleHistoryLineCount() const;
  int visibleSuggestionLineCount() const;
  bool tryGetInputSelectionRect(glm::ivec4& outRect) const;
  bool tryGetInputCaretRect(glm::ivec4& outRect) const;
  bool isMouseInsideInput(float mouseX, float mouseY) const;
  void beginMouseSelection(float mouseX);
  void updateMouseSelection(float mouseX);
  void endMouseSelection();

private:
  struct ResolvedInputLayout {
    std::string lineText;
    std::size_t visibleStart = 0;
    std::size_t visibleEnd = 0;
    float caretPixelOffset = 0.0f;
    float selectionPixelStart = 0.0f;
    float selectionPixelEnd = 0.0f;
    float textHeight = 0.0f;
    int textX = 0;
    int textY = 0;
    bool showCaret = false;
    bool showSelection = false;
    bool valid = false;
  };

  struct Entry {
    std::string text;
    double timestampSeconds = 0.0;
  };

  static constexpr int kMaxHistoryEntries = 32;
  static constexpr int kVisibleHistoryLines = 6;
  static constexpr int kVisibleSuggestionLines = 4;

  bool open_ = false;
  TextInputState inputState_;
  std::string lastSubmittedInput_;
  std::deque<Entry> history_;
  hudWatchText* inputLineElement_ = nullptr;

  void pushHistory(const std::string& text);
  void submit(GameChatCommandContext& commandContext);
  bool shouldShowHistory() const;
  bool shouldShowSuggestions() const;
  void autocompleteInput();
  std::vector<std::string> autocompleteCandidates() const;
  std::vector<std::string> visibleHistoryLines() const;
  ResolvedInputLayout resolveInputLayout() const;
  static std::vector<std::string> wrapChatText(const std::string& text,
                                               std::size_t maxCharactersPerLine);

  static std::string trim(const std::string& value);
  static std::string lowercase(std::string value);
  static std::string longestCommonPrefix(const std::vector<std::string>& values);
  static bool tryParsePositiveAmount(const std::string& value, int& outAmount);
  static bool tryParseBooleanWord(const std::string& value, bool& outValue);

  bool executeCommand(GameChatCommandContext& commandContext,
                      const std::string& commandLine);
  bool executeGetCommand(GameChatCommandContext& commandContext,
                         const std::string& arguments);
  bool executeDebugCommand(GameChatCommandContext& commandContext,
                           const std::string& arguments);
  bool executeSummonCommand(GameChatCommandContext& commandContext,
                            const std::string& arguments);
  bool executeSandboxCommand(GameChatCommandContext& commandContext,
                             const std::string& arguments);
};
