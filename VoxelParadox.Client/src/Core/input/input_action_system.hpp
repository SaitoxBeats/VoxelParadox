#pragma once

#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "input/input_binding.hpp"

namespace InputMapping {

struct InputActionDefinition {
  std::string id{};
  std::string label{};
  std::string categoryId{};
  InputContext context = InputContext::Gameplay;
  InputBindingType inputType = InputBindingType::Keyboard;
  InputBinding defaultBinding{};
};

struct InputCategoryDefinition {
  std::string id{};
  std::string label{};
  std::vector<std::string> actionIds{};
};

struct InputActionState {
  InputBinding effectiveBinding{};
  ResolvedInputBinding resolvedBinding{};
  bool overridden = false;
};

struct ControlsCatalog {
  std::vector<InputCategoryDefinition> categories{};
  std::vector<InputActionDefinition> actions{};
};

class InputActionSystem {
public:
  static InputActionSystem& instance();

  bool initialize(std::string* outStatus = nullptr);
  void shutdown();
  bool isInitialized() const;

  void setActiveContexts(std::initializer_list<InputContext> contexts);
  void setActiveContexts(const std::vector<InputContext>& contexts);
  void clearActiveContexts();

  void setCaptureMode(bool enabled);
  bool isCaptureMode() const;

  bool isDown(std::string_view actionId) const;
  bool wasPressed(std::string_view actionId) const;

  bool tryCaptureBinding(InputBinding& outBinding,
                         bool ignoreMouseLeft = false) const;

  std::string bindingDisplayText(
      std::string_view actionId,
      const ControlBindingOverrides* overrides = nullptr) const;
  bool isBindingCompatible(std::string_view actionId,
                           const InputBinding& binding) const;
  bool trySetBinding(std::string_view actionId, const InputBinding& binding,
                     ControlBindingOverrides& overrides,
                     ConflictInfo* outConflict = nullptr) const;
  void clearOverride(std::string_view actionId,
                     ControlBindingOverrides& overrides) const;
  void resetCategory(std::string_view categoryId,
                     ControlBindingOverrides& overrides) const;
  void resetAll(ControlBindingOverrides& overrides) const;
  void sanitizeOverrides(ControlBindingOverrides& overrides) const;

  void applyOverrides(const ControlBindingOverrides& overrides);

  const ControlsCatalog& catalog() const;
  const InputActionDefinition* findAction(std::string_view actionId) const;
  const InputCategoryDefinition* findCategory(std::string_view categoryId) const;
  std::vector<const InputActionDefinition*> actionsForCategory(
      std::string_view categoryId) const;

private:
  InputActionSystem() = default;

  struct CatalogBuildData {
    ControlsCatalog catalog{};
    std::string status{};
    bool usedFallback = false;
  };

  ControlsCatalog catalog_{};
  std::vector<InputActionState> liveStates_{};
  ControlBindingOverrides liveOverrides_{};
  std::unordered_map<std::string, std::size_t> actionIndexById_{};
  std::unordered_map<std::string, std::size_t> categoryIndexById_{};
  std::vector<InputContext> activeContexts_{};
  bool initialized_ = false;
  bool captureMode_ = false;

  static CatalogBuildData loadCatalog();
  static ControlsCatalog makeFallbackCatalog();

  void rebuildCatalogLookups();
  void rebuildLiveStates();

  const InputActionState* findLiveState(std::string_view actionId) const;
  static std::optional<ResolvedInputBinding> resolveBinding(
      const InputBinding& binding);
  InputBinding effectiveBindingForAction(
      std::size_t actionIndex,
      const ControlBindingOverrides* overrides) const;
  bool isContextActive(InputContext context) const;
  bool isShadowedByHigherContext(std::size_t actionIndex) const;
  bool queryBindingState(std::string_view actionId, bool pressed) const;
};

} // namespace InputMapping
