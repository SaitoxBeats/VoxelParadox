#pragma once

#include <deque>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/support/text_input.hpp"

class Player;
class WorldStack;
class hudWatchText;

namespace Gameplay {
class EventQueue;
}

struct GameChatTextSegment {
  std::string text;
  glm::vec3 color{1.0f};
};

namespace GameChatTheme {
extern const glm::vec3 kDefaultHistoryColor;
extern const glm::vec3 kVersalNotificationColor;
} // namespace GameChatTheme

struct GameChatCommandContext {
  Player& player;
  WorldStack& worldStack;
  bool& wireframeMode;
  bool& debugThirdPersonView;
  Gameplay::EventQueue* eventQueue = nullptr;
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
  void pushNotification(std::vector<GameChatTextSegment> segments);
  void pushNotification(std::string text);
  void pushFirstVersalNotification();

  void setupHud();
  void syncHudState() const;

  std::string historyLineText(int lineIndex) const;
  std::string inputLineText() const;
  std::string suggestionLineText(int lineIndex) const;
  int visibleHistoryLineCount() const;
  int visibleSuggestionLineCount() const;
  float visibleTopBackgroundContentWidth() const;
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
    std::vector<GameChatTextSegment> segments;
    double timestampSeconds = 0.0;
  };

  struct HistoryLine {
    std::vector<GameChatTextSegment> segments;
  };

  static constexpr int kMaxHistoryEntries = 32;
  static constexpr int kVisibleHistoryLines = 6;
  static constexpr int kVisibleSuggestionLines = 4;
  static constexpr int kMaxHistorySegmentsPerLine = 16;

  bool open_ = false;
  TextInputState inputState_;
  std::string lastSubmittedInput_;
  std::deque<Entry> history_;
  int historyScrollOffset_ = 0;
  float historyScrollRemainder_ = 0.0f;
  hudWatchText* historyMeasureElement_ = nullptr;
  hudWatchText* inputLineElement_ = nullptr;

  void pushHistory(const std::string& text);
  void pushHistory(std::vector<GameChatTextSegment> segments);
  std::vector<HistoryLine> allHistoryRichLines() const;
  int historyScrollLimit() const;
  int adjustHistoryScroll(int lineDelta);
  void submit(GameChatCommandContext& commandContext);
  bool shouldShowHistory() const;
  bool shouldShowSuggestions() const;
  void autocompleteInput();
  std::vector<std::string> autocompleteCandidates() const;
  std::vector<std::string> visibleHistoryLines() const;
  std::vector<HistoryLine> visibleHistoryRichLines() const;
  std::string historyLineSegmentText(int lineIndex, int segmentIndex) const;
  glm::vec3 historyLineSegmentColor(int lineIndex, int segmentIndex) const;
  float historyLineSegmentOffset(int lineIndex, int segmentIndex,
                                 const hudWatchText& textElement) const;
  ResolvedInputLayout resolveInputLayout() const;
  static std::vector<std::string> wrapChatText(const std::string& text,
                                               std::size_t maxCharactersPerLine);
  static std::vector<HistoryLine> wrapChatSegments(
      const std::vector<GameChatTextSegment>& segments,
      std::size_t maxCharactersPerLine);
  static std::string plainTextForLine(const HistoryLine& line);

  static std::string trim(const std::string& value);
  static std::string lowercase(std::string value);
  static std::string longestCommonPrefix(const std::vector<std::string>& values);
  static bool tryParsePositiveAmount(const std::string& value, int& outAmount);
  static bool tryParseNonNegativeInteger(const std::string& value, long long& outAmount);
  static bool tryParseNonNegativeNumber(const std::string& value, float& outAmount);
  static bool tryParseBooleanWord(const std::string& value, bool& outValue);

  bool executeCommand(GameChatCommandContext& commandContext,
                      const std::string& commandLine);
  bool executeGetCommand(GameChatCommandContext& commandContext,
                         const std::string& arguments);
  bool executeClearCommand(GameChatCommandContext& commandContext,
                           const std::string& arguments);
  bool executeSetCommand(GameChatCommandContext& commandContext,
                         const std::string& arguments);
  bool executeResetCommand(GameChatCommandContext& commandContext,
                           const std::string& arguments);
  bool executeDebugCommand(GameChatCommandContext& commandContext,
                           const std::string& arguments);
  bool executeSummonCommand(GameChatCommandContext& commandContext,
                            const std::string& arguments);
  bool executeSandboxCommand(GameChatCommandContext& commandContext,
                             const std::string& arguments);
};
