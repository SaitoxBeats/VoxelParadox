#include "input/input_action_system.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include "client_assets.hpp"
#include "engine/input.hpp"
#include "path/app_paths.hpp"

namespace InputMapping {
namespace {

struct KeyToken {
  const char* name = "";
  int code = -1;
};

constexpr std::array<KeyToken, 17> kSpecialKeyboardTokens{{
    {"Space", GLFW_KEY_SPACE},
    {"Escape", GLFW_KEY_ESCAPE},
    {"Enter", GLFW_KEY_ENTER},
    {"Tab", GLFW_KEY_TAB},
    {"Up", GLFW_KEY_UP},
    {"Down", GLFW_KEY_DOWN},
    {"Left", GLFW_KEY_LEFT},
    {"Right", GLFW_KEY_RIGHT},
    {"LeftShift", GLFW_KEY_LEFT_SHIFT},
    {"RightShift", GLFW_KEY_RIGHT_SHIFT},
    {"LeftControl", GLFW_KEY_LEFT_CONTROL},
    {"RightControl", GLFW_KEY_RIGHT_CONTROL},
    {"Delete", GLFW_KEY_DELETE},
    {"Comma", GLFW_KEY_COMMA},
    {"Period", GLFW_KEY_PERIOD},
    {"Minus", GLFW_KEY_MINUS},
    {"Equal", GLFW_KEY_EQUAL},
}};

bool getRequiredString(lua_State* luaState, int tableIndex, const char* fieldName,
                       std::string& outValue, std::string& outError) {
  lua_getfield(luaState, tableIndex, fieldName);
  if (!lua_isstring(luaState, -1)) {
    outError = std::string("Missing or invalid string field '") + fieldName + "'.";
    lua_pop(luaState, 1);
    return false;
  }

  outValue = lua_tostring(luaState, -1);
  lua_pop(luaState, 1);
  return true;
}

std::optional<int> resolveKeyboardToken(const std::string& token) {
  if (token.size() == 1) {
    const unsigned char ch = static_cast<unsigned char>(token[0]);
    if (std::isalpha(ch) != 0) {
      return GLFW_KEY_A + (std::toupper(ch) - 'A');
    }
    if (std::isdigit(ch) != 0) {
      return GLFW_KEY_0 + (ch - '0');
    }
  }

  for (const KeyToken& key : kSpecialKeyboardTokens) {
    if (token == key.name) {
      return key.code;
    }
  }

  if (token.size() >= 2 && token[0] == 'F') {
    const std::string numericSuffix = token.substr(1);
    bool numeric = !numericSuffix.empty();
    for (char ch : numericSuffix) {
      if (!std::isdigit(static_cast<unsigned char>(ch))) {
        numeric = false;
        break;
      }
    }
    if (numeric) {
      const int functionIndex = std::stoi(numericSuffix);
      if (functionIndex >= 1 && functionIndex <= 25) {
        return GLFW_KEY_F1 + (functionIndex - 1);
      }
    }
  }

  return std::nullopt;
}

std::optional<int> resolveMouseToken(const std::string& token) {
  if (token == "MouseLeft") {
    return GLFW_MOUSE_BUTTON_LEFT;
  }
  if (token == "MouseRight") {
    return GLFW_MOUSE_BUTTON_RIGHT;
  }
  if (token == "MouseMiddle") {
    return GLFW_MOUSE_BUTTON_MIDDLE;
  }
  return std::nullopt;
}

InputActionDefinition makeAction(const char* id, const char* label,
                                 const char* categoryId, InputContext context,
                                 InputBindingType inputType,
                                 const char* defaultBind) {
  InputActionDefinition action;
  action.id = id;
  action.label = label;
  action.categoryId = categoryId;
  action.context = context;
  action.inputType = inputType;
  action.defaultBinding.type = inputType;
  action.defaultBinding.codeName = defaultBind;
  return action;
}

void addCategory(ControlsCatalog& catalog, const char* id, const char* label,
                 std::initializer_list<InputActionDefinition> actions) {
  InputCategoryDefinition category;
  category.id = id;
  category.label = label;
  category.actionIds.reserve(actions.size());

  for (const InputActionDefinition& action : actions) {
    category.actionIds.push_back(action.id);
    catalog.actions.push_back(action);
  }

  catalog.categories.push_back(std::move(category));
}

} // namespace

InputActionSystem& InputActionSystem::instance() {
  static InputActionSystem system;
  return system;
}

bool InputActionSystem::initialize(std::string* outStatus) {
  if (initialized_) {
    if (outStatus) {
      outStatus->clear();
    }
    return true;
  }

  CatalogBuildData buildData = loadCatalog();
  catalog_ = std::move(buildData.catalog);
  rebuildCatalogLookups();
  rebuildLiveStates();
  initialized_ = true;

  if (outStatus) {
    *outStatus = buildData.status;
  }

  if (!buildData.status.empty()) {
    std::printf("[Controls] %s\n", buildData.status.c_str());
  } else {
    std::printf("[Controls] Loaded %zu actions from controls catalog.\n",
                catalog_.actions.size());
  }

  return true;
}

void InputActionSystem::shutdown() {
  catalog_ = {};
  liveStates_.clear();
  liveOverrides_.clear();
  actionIndexById_.clear();
  categoryIndexById_.clear();
  activeContexts_.clear();
  captureMode_ = false;
  initialized_ = false;
}

bool InputActionSystem::isInitialized() const { return initialized_; }

void InputActionSystem::setActiveContexts(
    std::initializer_list<InputContext> contexts) {
  activeContexts_.assign(contexts.begin(), contexts.end());
}

void InputActionSystem::setActiveContexts(const std::vector<InputContext>& contexts) {
  activeContexts_ = contexts;
}

void InputActionSystem::clearActiveContexts() { activeContexts_.clear(); }

void InputActionSystem::setCaptureMode(bool enabled) { captureMode_ = enabled; }

bool InputActionSystem::isCaptureMode() const { return captureMode_; }

bool InputActionSystem::isDown(std::string_view actionId) const {
  return queryBindingState(actionId, false);
}

bool InputActionSystem::wasPressed(std::string_view actionId) const {
  return queryBindingState(actionId, true);
}

bool InputActionSystem::tryCaptureBinding(InputBinding& outBinding,
                                          bool ignoreMouseLeft) const {
  for (int key = 0; key < 512; ++key) {
    if (!Input::keyPressed(key)) {
      continue;
    }

    outBinding = {};
    outBinding.type = InputBindingType::Keyboard;

    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
      outBinding.codeName = std::string(1, static_cast<char>('A' + (key - GLFW_KEY_A)));
      return true;
    }
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) {
      outBinding.codeName = std::string(1, static_cast<char>('0' + (key - GLFW_KEY_0)));
      return true;
    }
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F25) {
      outBinding.codeName = "F" + std::to_string(key - GLFW_KEY_F1 + 1);
      return true;
    }

    for (const KeyToken& token : kSpecialKeyboardTokens) {
      if (token.code == key) {
        outBinding.codeName = token.name;
        return true;
      }
    }
  }

  for (int button = 0; button < 8; ++button) {
    if (!Input::mousePressed(button)) {
      continue;
    }
    if (ignoreMouseLeft && button == GLFW_MOUSE_BUTTON_LEFT) {
      continue;
    }

    outBinding = {};
    outBinding.type = InputBindingType::MouseButton;
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
      outBinding.codeName = "MouseLeft";
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
      outBinding.codeName = "MouseRight";
    } else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
      outBinding.codeName = "MouseMiddle";
    } else {
      continue;
    }
    return true;
  }

  return false;
}

std::string InputActionSystem::bindingDisplayText(
    std::string_view actionId,
    const ControlBindingOverrides* overrides) const {
  const auto actionIt = actionIndexById_.find(std::string(actionId));
  if (actionIt == actionIndexById_.end()) {
    return "Unbound";
  }

  return InputMapping::bindingDisplayText(
      effectiveBindingForAction(actionIt->second, overrides));
}

bool InputActionSystem::isBindingCompatible(std::string_view actionId,
                                            const InputBinding& binding) const {
  const InputActionDefinition* action = findAction(actionId);
  if (!action || binding.empty()) {
    return false;
  }

  if (binding.type != action->inputType) {
    return false;
  }

  return resolveBinding(binding).has_value();
}

bool InputActionSystem::trySetBinding(std::string_view actionId,
                                      const InputBinding& binding,
                                      ControlBindingOverrides& overrides,
                                      ConflictInfo* outConflict) const {
  const InputActionDefinition* action = findAction(actionId);
  if (!action || !isBindingCompatible(actionId, binding)) {
    return false;
  }

  if (outConflict) {
    *outConflict = {};
  }

  for (const InputActionDefinition& otherAction : catalog_.actions) {
    if (otherAction.id == action->id || otherAction.context != action->context) {
      continue;
    }

    const auto otherIt = actionIndexById_.find(otherAction.id);
    if (otherIt == actionIndexById_.end()) {
      continue;
    }

    const InputBinding otherBinding =
        effectiveBindingForAction(otherIt->second, &overrides);
    if (!sameBinding(binding, otherBinding)) {
      continue;
    }

    if (outConflict) {
      outConflict->actionId = otherAction.id;
      outConflict->actionLabel = otherAction.label;
      outConflict->categoryId = otherAction.categoryId;
      outConflict->context = otherAction.context;
    }
    return false;
  }

  if (sameBinding(binding, action->defaultBinding)) {
    overrides.erase(action->id);
  } else {
    overrides[action->id] = binding;
  }

  return true;
}

void InputActionSystem::clearOverride(std::string_view actionId,
                                      ControlBindingOverrides& overrides) const {
  overrides.erase(std::string(actionId));
}

void InputActionSystem::resetCategory(std::string_view categoryId,
                                      ControlBindingOverrides& overrides) const {
  const InputCategoryDefinition* category = findCategory(categoryId);
  if (!category) {
    return;
  }

  for (const std::string& actionId : category->actionIds) {
    overrides.erase(actionId);
  }
}

void InputActionSystem::resetAll(ControlBindingOverrides& overrides) const {
  overrides.clear();
}

void InputActionSystem::sanitizeOverrides(ControlBindingOverrides& overrides) const {
  for (auto it = overrides.begin(); it != overrides.end();) {
    const InputActionDefinition* action = findAction(it->first);
    if (!action || !isBindingCompatible(it->first, it->second) ||
        sameBinding(it->second, action->defaultBinding)) {
      it = overrides.erase(it);
      continue;
    }

    ++it;
  }
}

void InputActionSystem::applyOverrides(const ControlBindingOverrides& overrides) {
  liveOverrides_ = overrides;
  sanitizeOverrides(liveOverrides_);
  rebuildLiveStates();
}

const ControlsCatalog& InputActionSystem::catalog() const { return catalog_; }

const InputActionDefinition* InputActionSystem::findAction(
    std::string_view actionId) const {
  const auto it = actionIndexById_.find(std::string(actionId));
  if (it == actionIndexById_.end()) {
    return nullptr;
  }

  return &catalog_.actions[it->second];
}

const InputCategoryDefinition* InputActionSystem::findCategory(
    std::string_view categoryId) const {
  const auto it = categoryIndexById_.find(std::string(categoryId));
  if (it == categoryIndexById_.end()) {
    return nullptr;
  }

  return &catalog_.categories[it->second];
}

std::vector<const InputActionDefinition*> InputActionSystem::actionsForCategory(
    std::string_view categoryId) const {
  std::vector<const InputActionDefinition*> actions;
  const InputCategoryDefinition* category = findCategory(categoryId);
  if (!category) {
    return actions;
  }

  actions.reserve(category->actionIds.size());
  for (const std::string& actionId : category->actionIds) {
    if (const InputActionDefinition* action = findAction(actionId)) {
      actions.push_back(action);
    }
  }

  return actions;
}

InputActionSystem::CatalogBuildData InputActionSystem::loadCatalog() {
  CatalogBuildData buildData;
  const std::filesystem::path catalogPath =
      AppPaths::gameDataRoot() / "Config" / "controls.lua";
  const std::filesystem::path fallbackCatalogPath =
      AppPaths::resolve(ClientAssets::kControlsConfig);
  const std::filesystem::path& loadPath =
      std::filesystem::exists(catalogPath) ? catalogPath : fallbackCatalogPath;

  lua_State* luaState = luaL_newstate();
  if (!luaState) {
    buildData.catalog = makeFallbackCatalog();
    buildData.status =
        "Failed to create Lua state for controls catalog. Using fallback controls.";
    buildData.usedFallback = true;
    return buildData;
  }

  luaL_openlibs(luaState);
  const int loadResult = luaL_loadfile(luaState, loadPath.string().c_str());
  const int callResult =
      loadResult == LUA_OK ? lua_pcall(luaState, 0, 1, 0) : loadResult;

  if (callResult != LUA_OK || !lua_istable(luaState, -1)) {
    const std::string luaError =
        lua_isstring(luaState, -1) ? lua_tostring(luaState, -1)
                                   : "Unknown Lua controls error.";
    buildData.catalog = makeFallbackCatalog();
    buildData.status =
        "Failed to load controls catalog from " + loadPath.string() +
        ". Using fallback controls. Reason: " + luaError;
    buildData.usedFallback = true;
    lua_close(luaState);
    return buildData;
  }

  std::string parseError;
  lua_getfield(luaState, -1, "categories");
  if (!lua_istable(luaState, -1)) {
    parseError = "Top-level field 'categories' must be an array.";
  } else {
    const lua_Integer categoryCount = luaL_len(luaState, -1);
    buildData.catalog.categories.reserve(static_cast<std::size_t>(categoryCount));

    for (lua_Integer categoryIndex = 1;
         categoryIndex <= categoryCount && parseError.empty(); ++categoryIndex) {
      lua_geti(luaState, -1, categoryIndex);
      if (!lua_istable(luaState, -1)) {
        parseError = "Each category entry must be a table.";
        lua_pop(luaState, 1);
        break;
      }

      InputCategoryDefinition category;
      if (!getRequiredString(luaState, -1, "id", category.id, parseError) ||
          !getRequiredString(luaState, -1, "label", category.label, parseError)) {
        lua_pop(luaState, 1);
        break;
      }

      lua_getfield(luaState, -1, "actions");
      if (!lua_istable(luaState, -1)) {
        parseError = "Each category must define an 'actions' array.";
        lua_pop(luaState, 2);
        break;
      }

      const lua_Integer actionCount = luaL_len(luaState, -1);
      category.actionIds.reserve(static_cast<std::size_t>(actionCount));

      for (lua_Integer actionIndex = 1;
           actionIndex <= actionCount && parseError.empty(); ++actionIndex) {
        lua_geti(luaState, -1, actionIndex);
        if (!lua_istable(luaState, -1)) {
          parseError = "Each action entry must be a table.";
          lua_pop(luaState, 1);
          break;
        }

        InputActionDefinition action;
        std::string contextToken;
        std::string inputTypeToken;
        std::string defaultBind;
        std::string categoryToken;
        if (!getRequiredString(luaState, -1, "id", action.id, parseError) ||
            !getRequiredString(luaState, -1, "label", action.label, parseError) ||
            !getRequiredString(luaState, -1, "category", categoryToken, parseError) ||
            !getRequiredString(luaState, -1, "context", contextToken, parseError) ||
            !getRequiredString(luaState, -1, "input_type", inputTypeToken, parseError) ||
            !getRequiredString(luaState, -1, "default_bind", defaultBind, parseError)) {
          lua_pop(luaState, 1);
          break;
        }

        if (categoryToken != category.id) {
          parseError = "Action '" + action.id +
                       "' has a category that does not match its parent.";
          lua_pop(luaState, 1);
          break;
        }

        action.categoryId = category.id;
        action.context = parseInputContextToken(contextToken);
        action.inputType = parseInputBindingTypeToken(inputTypeToken);
        action.defaultBinding.type = action.inputType;
        action.defaultBinding.codeName = defaultBind;

        if (action.inputType == InputBindingType::Invalid) {
          parseError = "Action '" + action.id + "' uses an invalid input_type.";
          lua_pop(luaState, 1);
          break;
        }

        const auto resolvedDefault = resolveBinding(action.defaultBinding);
        if (!resolvedDefault.has_value()) {
          parseError = "Action '" + action.id +
                       "' uses an unsupported default_bind token.";
          lua_pop(luaState, 1);
          break;
        }

        category.actionIds.push_back(action.id);
        buildData.catalog.actions.push_back(std::move(action));
        lua_pop(luaState, 1);
      }

      lua_pop(luaState, 1);
      if (!parseError.empty()) {
        lua_pop(luaState, 1);
        break;
      }

      buildData.catalog.categories.push_back(std::move(category));
      lua_pop(luaState, 1);
    }
  }

  lua_pop(luaState, 1);
  lua_close(luaState);

  if (!parseError.empty()) {
    buildData.catalog = makeFallbackCatalog();
    buildData.status =
        "Failed to parse controls catalog from " + loadPath.string() +
        ". Using fallback controls. Reason: " + parseError;
    buildData.usedFallback = true;
    return buildData;
  }

  buildData.status =
      "Loaded controls catalog from " + loadPath.string() + ".";
  return buildData;
}

ControlsCatalog InputActionSystem::makeFallbackCatalog() {
  ControlsCatalog catalog;

  addCategory(
      catalog, "movement", "Movement",
      {
          makeAction("move_forward", "Move Forward", "movement",
                     InputContext::Gameplay, InputBindingType::Keyboard, "W"),
          makeAction("move_backward", "Move Backward", "movement",
                     InputContext::Gameplay, InputBindingType::Keyboard, "S"),
          makeAction("move_left", "Move Left", "movement",
                     InputContext::Gameplay, InputBindingType::Keyboard, "A"),
          makeAction("move_right", "Move Right", "movement",
                     InputContext::Gameplay, InputBindingType::Keyboard, "D"),
          makeAction("jump", "Jump", "movement", InputContext::Gameplay,
                     InputBindingType::Keyboard, "Space"),
          makeAction("crouch", "Crouch", "movement", InputContext::Gameplay,
                     InputBindingType::Keyboard, "LeftShift"),
          makeAction("run", "Run", "movement", InputContext::Gameplay,
                     InputBindingType::Keyboard, "LeftControl"),
          makeAction("zoom", "Zoom", "movement", InputContext::Gameplay,
                     InputBindingType::Keyboard, "C"),
      });

  addCategory(
      catalog, "interaction", "Interaction",
      {
          makeAction("break_block", "Break Block", "interaction",
                     InputContext::Gameplay, InputBindingType::MouseButton,
                     "MouseLeft"),
          makeAction("place_block", "Place Block", "interaction",
                     InputContext::Gameplay, InputBindingType::MouseButton,
                     "MouseRight"),
          makeAction("preview_portal", "Preview Portal", "interaction",
                     InputContext::Gameplay, InputBindingType::Keyboard, "V"),
          makeAction("enter_portal", "Enter Portal", "interaction",
                     InputContext::Gameplay, InputBindingType::Keyboard, "F"),
          makeAction("ascend_dimension", "Ascend Dimension", "interaction",
                     InputContext::Gameplay, InputBindingType::Keyboard, "R"),
      });

  addCategory(
      catalog, "hotbar", "Hotbar",
      {
          makeAction("hotbar_slot_1", "Slot 1", "hotbar",
                     InputContext::Gameplay, InputBindingType::Keyboard, "1"),
          makeAction("hotbar_slot_2", "Slot 2", "hotbar",
                     InputContext::Gameplay, InputBindingType::Keyboard, "2"),
          makeAction("hotbar_slot_3", "Slot 3", "hotbar",
                     InputContext::Gameplay, InputBindingType::Keyboard, "3"),
          makeAction("hotbar_slot_4", "Slot 4", "hotbar",
                     InputContext::Gameplay, InputBindingType::Keyboard, "4"),
          makeAction("hotbar_slot_5", "Slot 5", "hotbar",
                     InputContext::Gameplay, InputBindingType::Keyboard, "5"),
          makeAction("hotbar_slot_6", "Slot 6", "hotbar",
                     InputContext::Gameplay, InputBindingType::Keyboard, "6"),
          makeAction("hotbar_slot_7", "Slot 7", "hotbar",
                     InputContext::Gameplay, InputBindingType::Keyboard, "7"),
          makeAction("hotbar_slot_8", "Slot 8", "hotbar",
                     InputContext::Gameplay, InputBindingType::Keyboard, "8"),
          makeAction("hotbar_slot_9", "Slot 9", "hotbar",
                     InputContext::Gameplay, InputBindingType::Keyboard, "9"),
      });

  addCategory(
      catalog, "interface", "Interface",
      {
          makeAction("toggle_inventory", "Toggle Inventory", "interface",
                     InputContext::Ui, InputBindingType::Keyboard, "E"),
          makeAction("toggle_pause", "Toggle Pause", "interface",
                     InputContext::Ui, InputBindingType::Keyboard, "Escape"),
          makeAction("open_chat", "Open Chat", "interface", InputContext::Ui,
                     InputBindingType::Keyboard, "T"),
          makeAction("toggle_portal_tracker", "Portal Tracker", "interface",
                     InputContext::Ui, InputBindingType::Keyboard, "P"),
      });

  addCategory(
      catalog, "system", "System",
      {
          makeAction("take_screenshot", "Take Screenshot", "system",
                     InputContext::Ui, InputBindingType::Keyboard, "F2"),
          makeAction("toggle_hud", "Toggle HUD", "system", InputContext::Ui,
                     InputBindingType::Keyboard, "F1"),
          makeAction("toggle_fullscreen", "Toggle Fullscreen", "system",
                     InputContext::Ui, InputBindingType::Keyboard, "F11"),
      });

  addCategory(
      catalog, "ui_navigation", "UI Navigation",
      {
          makeAction("ui_accept", "Accept", "ui_navigation",
                     InputContext::UiNavigation, InputBindingType::Keyboard,
                     "Enter"),
          makeAction("ui_cancel", "Cancel", "ui_navigation",
                     InputContext::UiNavigation, InputBindingType::Keyboard,
                     "Escape"),
          makeAction("ui_up", "Move Up", "ui_navigation",
                      InputContext::UiNavigation, InputBindingType::Keyboard, "Up"),
          makeAction("ui_down", "Move Down", "ui_navigation",
                      InputContext::UiNavigation, InputBindingType::Keyboard,
                      "Down"),
          makeAction("ui_left", "Move Left", "ui_navigation",
                     InputContext::UiNavigation, InputBindingType::Keyboard,
                     "Left"),
          makeAction("ui_right", "Move Right", "ui_navigation",
                     InputContext::UiNavigation, InputBindingType::Keyboard,
                     "Right"),
          makeAction("ui_delete", "Delete", "ui_navigation",
                     InputContext::UiNavigation, InputBindingType::Keyboard,
                     "Delete"),
      });

  return catalog;
}

void InputActionSystem::rebuildCatalogLookups() {
  actionIndexById_.clear();
  categoryIndexById_.clear();

  for (std::size_t index = 0; index < catalog_.categories.size(); ++index) {
    categoryIndexById_[catalog_.categories[index].id] = index;
  }

  for (std::size_t index = 0; index < catalog_.actions.size(); ++index) {
    actionIndexById_[catalog_.actions[index].id] = index;
  }
}

void InputActionSystem::rebuildLiveStates() {
  liveStates_.assign(catalog_.actions.size(), {});

  for (std::size_t actionIndex = 0; actionIndex < catalog_.actions.size();
       ++actionIndex) {
    InputActionState state;
    state.effectiveBinding =
        effectiveBindingForAction(actionIndex, &liveOverrides_);
    state.resolvedBinding =
        resolveBinding(state.effectiveBinding).value_or(ResolvedInputBinding{});
    state.overridden =
        liveOverrides_.find(catalog_.actions[actionIndex].id) !=
        liveOverrides_.end();
    liveStates_[actionIndex] = std::move(state);
  }
}

const InputActionState* InputActionSystem::findLiveState(
    std::string_view actionId) const {
  const auto actionIt = actionIndexById_.find(std::string(actionId));
  if (actionIt == actionIndexById_.end() ||
      actionIt->second >= liveStates_.size()) {
    return nullptr;
  }

  return &liveStates_[actionIt->second];
}

std::optional<ResolvedInputBinding> InputActionSystem::resolveBinding(
    const InputBinding& binding) {
  if (binding.empty()) {
    return std::nullopt;
  }

  ResolvedInputBinding resolved;
  resolved.type = binding.type;
  if (binding.type == InputBindingType::Keyboard) {
    const std::optional<int> code = resolveKeyboardToken(binding.codeName);
    if (!code.has_value()) {
      return std::nullopt;
    }
    resolved.code = *code;
    resolved.valid = true;
    return resolved;
  }

  if (binding.type == InputBindingType::MouseButton) {
    const std::optional<int> code = resolveMouseToken(binding.codeName);
    if (!code.has_value()) {
      return std::nullopt;
    }
    resolved.code = *code;
    resolved.valid = true;
    return resolved;
  }

  return std::nullopt;
}

InputBinding InputActionSystem::effectiveBindingForAction(
    std::size_t actionIndex, const ControlBindingOverrides* overrides) const {
  const InputActionDefinition& action = catalog_.actions[actionIndex];
  if (overrides) {
    const auto overrideIt = overrides->find(action.id);
    if (overrideIt != overrides->end()) {
      return overrideIt->second;
    }
  }
  return action.defaultBinding;
}

bool InputActionSystem::isContextActive(InputContext context) const {
  return std::find(activeContexts_.begin(), activeContexts_.end(), context) !=
         activeContexts_.end();
}

bool InputActionSystem::isShadowedByHigherContext(std::size_t actionIndex) const {
  if (actionIndex >= catalog_.actions.size() || actionIndex >= liveStates_.size()) {
    return true;
  }

  const InputActionDefinition& action = catalog_.actions[actionIndex];
  const auto contextIt =
      std::find(activeContexts_.begin(), activeContexts_.end(), action.context);
  if (contextIt == activeContexts_.end()) {
    return true;
  }

  const InputActionState& state = liveStates_[actionIndex];
  if (!state.resolvedBinding.valid) {
    return true;
  }

  for (auto it = activeContexts_.begin(); it != contextIt; ++it) {
    for (std::size_t otherIndex = 0; otherIndex < catalog_.actions.size();
         ++otherIndex) {
      if (catalog_.actions[otherIndex].context != *it) {
        continue;
      }
      if (sameResolvedBinding(state.resolvedBinding,
                              liveStates_[otherIndex].resolvedBinding)) {
        return true;
      }
    }
  }

  return false;
}

bool InputActionSystem::queryBindingState(std::string_view actionId,
                                          bool pressed) const {
  if (!initialized_ || captureMode_) {
    return false;
  }

  const auto actionIt = actionIndexById_.find(std::string(actionId));
  if (actionIt == actionIndexById_.end()) {
    return false;
  }

  const std::size_t actionIndex = actionIt->second;
  if (actionIndex >= liveStates_.size() ||
      isShadowedByHigherContext(actionIndex)) {
    return false;
  }

  const ResolvedInputBinding& binding = liveStates_[actionIndex].resolvedBinding;
  if (!binding.valid) {
    return false;
  }

  if (binding.type == InputBindingType::Keyboard) {
    return pressed ? Input::keyPressed(binding.code) : Input::keyDown(binding.code);
  }
  if (binding.type == InputBindingType::MouseButton) {
    return pressed ? Input::mousePressed(binding.code)
                   : Input::mouseDown(binding.code);
  }
  return false;
}

} // namespace InputMapping
