#pragma once

// 1. Standard
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <functional>
#include <limits>
#include <string>
#include <utility>

// 2. Project
#include "engine/engine.hpp"
#include "engine/input.hpp"

struct TextInputState {
  std::string text;
  std::size_t caretIndex = 0;
  std::size_t selectionStart = 0;
  std::size_t selectionEnd = 0;
  std::size_t mouseSelectionAnchor = 0;
  double nextBackspaceRepeatTime = -1.0;
  double nextDeleteRepeatTime = -1.0;
  double nextLeftRepeatTime = -1.0;
  double nextRightRepeatTime = -1.0;
  bool mouseSelecting = false;

  bool empty() const { return text.empty(); }

  bool hasSelection() const { return selectionStart != selectionEnd; }

  std::size_t selectionFirst() const {
    return std::min(selectionStart, selectionEnd);
  }

  std::size_t selectionLast() const {
    return std::max(selectionStart, selectionEnd);
  }

  void clampCaret() {
    caretIndex = std::min(caretIndex, text.size());
    selectionStart = std::min(selectionStart, text.size());
    selectionEnd = std::min(selectionEnd, text.size());
    mouseSelectionAnchor = std::min(mouseSelectionAnchor, text.size());
  }

  void resetRepeats() {
    nextBackspaceRepeatTime = -1.0;
    nextDeleteRepeatTime = -1.0;
    nextLeftRepeatTime = -1.0;
    nextRightRepeatTime = -1.0;
  }

  void clearSelection() {
    clampCaret();
    selectionStart = caretIndex;
    selectionEnd = caretIndex;
    mouseSelectionAnchor = caretIndex;
  }

  void setText(const std::string& value) {
    text = value;
    caretIndex = text.size();
    clearSelection();
  }

  void selectAll() {
    selectionStart = 0;
    selectionEnd = text.size();
    caretIndex = selectionEnd;
    mouseSelectionAnchor = selectionStart;
  }

  void deleteSelection() {
    if (!hasSelection()) {
      return;
    }

    const std::size_t first = selectionFirst();
    const std::size_t last = selectionLast();
    text.erase(first, last - first);
    caretIndex = first;
    clearSelection();
  }

  void insertText(const std::string& typed, std::size_t maxLength = 0) {
    if (typed.empty()) {
      return;
    }

    if (hasSelection()) {
      deleteSelection();
    }

    const std::size_t available =
        maxLength == 0
            ? typed.size()
            : (maxLength > text.size() ? maxLength - text.size() : 0);
    if (available == 0) {
      return;
    }

    const std::string clipped = typed.substr(0, available);
    text.insert(caretIndex, clipped);
    caretIndex += clipped.size();
    clearSelection();
  }

  static bool isWordCharacter(char value) {
    return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '_';
  }

  std::size_t selectionAnchor() const {
    if (!hasSelection()) {
      return caretIndex;
    }

    return caretIndex == selectionStart ? selectionEnd : selectionStart;
  }

  void moveCaretTo(std::size_t index, bool keepSelection = false,
                   std::size_t anchorIndex = std::numeric_limits<std::size_t>::max()) {
    caretIndex = std::min(index, text.size());

    if (!keepSelection) {
      clearSelection();
      return;
    }

    const std::size_t resolvedAnchor =
        anchorIndex == std::numeric_limits<std::size_t>::max()
            ? selectionAnchor()
            : std::min(anchorIndex, text.size());

    selectionStart = resolvedAnchor;
    selectionEnd = caretIndex;
    mouseSelectionAnchor = resolvedAnchor;
  }

  std::size_t findWordBoundaryLeft(std::size_t index) const {
    index = std::min(index, text.size());

    while (index > 0 && !isWordCharacter(text[index - 1])) {
      --index;
    }

    while (index > 0 && isWordCharacter(text[index - 1])) {
      --index;
    }

    return index;
  }

  std::size_t findWordBoundaryRight(std::size_t index) const {
    index = std::min(index, text.size());

    while (index < text.size() && !isWordCharacter(text[index])) {
      ++index;
    }

    while (index < text.size() && isWordCharacter(text[index])) {
      ++index;
    }

    return index;
  }

  void moveCaretLeft(bool keepSelection = false) {
    if (!keepSelection && hasSelection()) {
      moveCaretTo(selectionFirst());
      return;
    }

    if (caretIndex == 0) {
      moveCaretTo(0, keepSelection);
      return;
    }

    const std::size_t anchor = keepSelection ? selectionAnchor() : caretIndex;
    moveCaretTo(caretIndex - 1, keepSelection, anchor);
  }

  void moveCaretRight(bool keepSelection = false) {
    if (!keepSelection && hasSelection()) {
      moveCaretTo(selectionLast());
      return;
    }

    if (caretIndex >= text.size()) {
      moveCaretTo(text.size(), keepSelection);
      return;
    }

    const std::size_t anchor = keepSelection ? selectionAnchor() : caretIndex;
    moveCaretTo(caretIndex + 1, keepSelection, anchor);
  }

  void moveCaretWordLeft(bool keepSelection = false) {
    if (!keepSelection && hasSelection()) {
      moveCaretTo(selectionFirst());
      return;
    }

    const std::size_t anchor = keepSelection ? selectionAnchor() : caretIndex;
    moveCaretTo(findWordBoundaryLeft(caretIndex), keepSelection, anchor);
  }

  void moveCaretWordRight(bool keepSelection = false) {
    if (!keepSelection && hasSelection()) {
      moveCaretTo(selectionLast());
      return;
    }

    const std::size_t anchor = keepSelection ? selectionAnchor() : caretIndex;
    moveCaretTo(findWordBoundaryRight(caretIndex), keepSelection, anchor);
  }

  void moveCaretHome(bool keepSelection = false) {
    const std::size_t anchor = keepSelection ? selectionAnchor() : caretIndex;
    moveCaretTo(0, keepSelection, anchor);
  }

  void moveCaretEnd(bool keepSelection = false) {
    const std::size_t anchor = keepSelection ? selectionAnchor() : caretIndex;
    moveCaretTo(text.size(), keepSelection, anchor);
  }

  void eraseBackward() {
    if (hasSelection()) {
      deleteSelection();
      return;
    }
    if (caretIndex == 0) {
      return;
    }
    text.erase(caretIndex - 1, 1);
    --caretIndex;
    clearSelection();
  }

  void eraseForward() {
    if (hasSelection()) {
      deleteSelection();
      return;
    }
    if (caretIndex >= text.size()) {
      return;
    }
    text.erase(caretIndex, 1);
    clearSelection();
  }

  void eraseWordBackward() {
    if (hasSelection()) {
      deleteSelection();
      return;
    }

    const std::size_t targetIndex = findWordBoundaryLeft(caretIndex);
    if (targetIndex >= caretIndex) {
      return;
    }

    text.erase(targetIndex, caretIndex - targetIndex);
    caretIndex = targetIndex;
    clearSelection();
  }

  void eraseWordForward() {
    if (hasSelection()) {
      deleteSelection();
      return;
    }

    const std::size_t targetIndex = findWordBoundaryRight(caretIndex);
    if (targetIndex <= caretIndex) {
      return;
    }

    text.erase(caretIndex, targetIndex - caretIndex);
    clearSelection();
  }

  std::string selectedText() const {
    if (!hasSelection()) {
      return {};
    }

    return text.substr(selectionFirst(), selectionLast() - selectionFirst());
  }

  bool copySelectionToClipboard() const {
    if (!hasSelection()) {
      return false;
    }

    Input::setClipboardText(selectedText());
    return true;
  }

  bool cutSelectionToClipboard() {
    if (!copySelectionToClipboard()) {
      return false;
    }

    deleteSelection();
    return true;
  }

  void pasteClipboard(std::size_t maxLength = 0) {
    insertText(Input::getClipboardText(), maxLength);
  }

  template <typename PrefixMeasureFn>
  std::size_t caretIndexFromMouse(float localX, PrefixMeasureFn&& measurePrefixWidth) const {
    if (text.empty()) {
      return 0;
    }

    std::size_t bestIndex = text.size();
    float bestDistance = std::numeric_limits<float>::max();

    for (std::size_t index = 0; index <= text.size(); ++index) {
      const float caretX = measurePrefixWidth(index);
      const float distance = std::fabs(localX - caretX);
      if (distance < bestDistance) {
        bestDistance = distance;
        bestIndex = index;
      }
    }

    return bestIndex;
  }

  template <typename PrefixMeasureFn>
  void placeCaretFromMouse(float localX, PrefixMeasureFn&& measurePrefixWidth) {
    moveCaretTo(caretIndexFromMouse(localX, std::forward<PrefixMeasureFn>(measurePrefixWidth)));
  }

  template <typename PrefixMeasureFn>
  void beginMouseSelection(float localX, PrefixMeasureFn&& measurePrefixWidth) {
    mouseSelectionAnchor =
        caretIndexFromMouse(localX, std::forward<PrefixMeasureFn>(measurePrefixWidth));
    mouseSelecting = true;
    moveCaretTo(mouseSelectionAnchor);
  }

  template <typename PrefixMeasureFn>
  void updateMouseSelection(float localX, PrefixMeasureFn&& measurePrefixWidth) {
    if (!mouseSelecting) {
      return;
    }

    moveCaretTo(
        caretIndexFromMouse(localX, std::forward<PrefixMeasureFn>(measurePrefixWidth)),
        true,
        mouseSelectionAnchor
    );
  }

  void endMouseSelection() {
    mouseSelecting = false;
    mouseSelectionAnchor = selectionAnchor();
  }

  bool handleKeyboardEditing(double now, std::size_t maxLength = 0) {
    clampCaret();

    const bool ctrlDown = Input::keyDown(GLFW_KEY_LEFT_CONTROL) ||
                          Input::keyDown(GLFW_KEY_RIGHT_CONTROL);
    const bool shiftDown = Input::keyDown(GLFW_KEY_LEFT_SHIFT) ||
                           Input::keyDown(GLFW_KEY_RIGHT_SHIFT);

    if (ctrlDown && Input::keyPressed(GLFW_KEY_A)) {
      selectAll();
    }

    if (ctrlDown && Input::keyPressed(GLFW_KEY_C)) {
      copySelectionToClipboard();
    }

    if (ctrlDown && Input::keyPressed(GLFW_KEY_X)) {
      cutSelectionToClipboard();
    }

    if (ctrlDown && Input::keyPressed(GLFW_KEY_V)) {
      pasteClipboard(maxLength);
    }

    if (Input::keyPressed(GLFW_KEY_HOME)) {
      moveCaretHome(shiftDown);
    }

    if (Input::keyPressed(GLFW_KEY_END)) {
      moveCaretEnd(shiftDown);
    }

    if (consumeHeldKey(GLFW_KEY_LEFT, now, nextLeftRepeatTime)) {
      if (ctrlDown) {
        moveCaretWordLeft(shiftDown);
      } else {
        moveCaretLeft(shiftDown);
      }
    }

    if (consumeHeldKey(GLFW_KEY_RIGHT, now, nextRightRepeatTime)) {
      if (ctrlDown) {
        moveCaretWordRight(shiftDown);
      } else {
        moveCaretRight(shiftDown);
      }
    }

    if (consumeHeldKey(GLFW_KEY_BACKSPACE, now, nextBackspaceRepeatTime)) {
      if (ctrlDown) {
        eraseWordBackward();
      } else {
        eraseBackward();
      }
    }

    if (consumeHeldKey(GLFW_KEY_DELETE, now, nextDeleteRepeatTime)) {
      if (ctrlDown) {
        eraseWordForward();
      } else {
        eraseForward();
      }
    }

    insertText(Input::consumeTypedChars(), maxLength);
    return true;
  }

  static bool consumeHeldKey(int key, double now, double& nextRepeatTime,
                             double initialDelay = 0.35,
                             double repeatInterval = 0.045) {
    if (Input::keyPressed(key)) {
      nextRepeatTime = now + initialDelay;
      return true;
    }
    if (!Input::keyDown(key)) {
      nextRepeatTime = -1.0;
      return false;
    }
    if (nextRepeatTime >= 0.0 && now >= nextRepeatTime) {
      nextRepeatTime = now + repeatInterval;
      return true;
    }
    return false;
  }
};
