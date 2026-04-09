#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace InputMapping {

enum class InputContext : std::uint8_t {
  Gameplay = 0,
  Ui,
  UiNavigation,
};

enum class InputBindingType : std::uint8_t {
  Invalid = 0,
  Keyboard,
  MouseButton,
};

struct InputBinding {
  InputBindingType type = InputBindingType::Invalid;
  std::string codeName{};

  bool empty() const {
    return type == InputBindingType::Invalid || codeName.empty();
  }
};

struct ResolvedInputBinding {
  InputBindingType type = InputBindingType::Invalid;
  int code = -1;
  bool valid = false;
};

struct ConflictInfo {
  std::string actionId{};
  std::string actionLabel{};
  std::string categoryId{};
  InputContext context = InputContext::Gameplay;
};

using ControlBindingOverrides = std::unordered_map<std::string, InputBinding>;

inline bool sameBinding(const InputBinding& a, const InputBinding& b) {
  return a.type == b.type && a.codeName == b.codeName;
}

inline bool sameResolvedBinding(const ResolvedInputBinding& a,
                                const ResolvedInputBinding& b) {
  return a.type == b.type && a.code == b.code && a.valid == b.valid;
}

inline const char* inputContextToken(InputContext context) {
  switch (context) {
  case InputContext::Gameplay:
    return "gameplay";
  case InputContext::Ui:
    return "ui";
  case InputContext::UiNavigation:
    return "ui_navigation";
  }

  return "gameplay";
}

inline InputContext parseInputContextToken(const std::string& token) {
  std::string lower = token;
  std::transform(
      lower.begin(), lower.end(), lower.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

  if (lower == "ui") {
    return InputContext::Ui;
  }
  if (lower == "ui_navigation") {
    return InputContext::UiNavigation;
  }
  return InputContext::Gameplay;
}

inline const char* inputBindingTypeToken(InputBindingType type) {
  switch (type) {
  case InputBindingType::Keyboard:
    return "keyboard";
  case InputBindingType::MouseButton:
    return "mouse_button";
  case InputBindingType::Invalid:
  default:
    return "invalid";
  }
}

inline InputBindingType parseInputBindingTypeToken(const std::string& token) {
  std::string lower = token;
  std::transform(
      lower.begin(), lower.end(), lower.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

  if (lower == "keyboard") {
    return InputBindingType::Keyboard;
  }
  if (lower == "mouse_button") {
    return InputBindingType::MouseButton;
  }
  return InputBindingType::Invalid;
}

inline std::string bindingDisplayText(const InputBinding& binding) {
  if (binding.empty()) {
    return "Unbound";
  }

  if (binding.codeName == "LeftShift") {
    return "Left Shift";
  }
  if (binding.codeName == "RightShift") {
    return "Right Shift";
  }
  if (binding.codeName == "LeftControl") {
    return "Left Ctrl";
  }
  if (binding.codeName == "RightControl") {
    return "Right Ctrl";
  }
  if (binding.codeName == "Escape") {
    return "Esc";
  }
  if (binding.codeName == "MouseLeft") {
    return "Mouse Left";
  }
  if (binding.codeName == "MouseRight") {
    return "Mouse Right";
  }
  if (binding.codeName == "MouseMiddle") {
    return "Mouse Middle";
  }

  return binding.codeName;
}

} // namespace InputMapping
