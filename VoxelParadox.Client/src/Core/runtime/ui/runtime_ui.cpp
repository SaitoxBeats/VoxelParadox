#include "runtime/ui/runtime_ui_internal.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

#include <GLFW/glfw3.h>
#include <imgui.h>

#include "enemies/enemy_spawn_system.hpp"
#include "input/input_action_ids.hpp"
#include "input/input_action_system.hpp"
#include "render/config/hand_animation_config.hpp"

namespace RuntimeUI {
namespace {

std::string enemySpawnTimerText(const WorldStack& worldStack) {
  const FractalWorld* world = worldStack.currentWorld();
  if (!world) {
    return "No active world";
  }

  const float cooldownSeconds = getRemainingNaturalEnemySpawnCooldownSeconds(*world);
  if (cooldownSeconds < 0.0f) {
    return "Scheduling first spawn...";
  }

  const int totalSeconds = std::max(0, static_cast<int>(std::ceil(cooldownSeconds)));
  const int hours = totalSeconds / 3600;
  const int minutes = (totalSeconds % 3600) / 60;
  const int seconds = totalSeconds % 60;

  char buffer[64];
  if (hours > 0) {
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", hours, minutes, seconds);
  } else {
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes, seconds);
  }
  return buffer;
}

struct DeveloperViewConfig {
  bool orientationGizmoEnabled = false;
  float orientationGizmoRadius = 42.0f;
  float orientationGizmoMargin = 28.0f;
};

DeveloperViewConfig gDeveloperViewConfig{};

struct GizmoAxis {
  const char* label = "";
  glm::vec3 worldDirection{0.0f};
  ImU32 color = 0;
  float depth = 0.0f;
  ImVec2 screenDirection{0.0f, 0.0f};
};

ImVec2 toImVec2(const glm::vec2& value) {
  return ImVec2(value.x, value.y);
}

ImVec2 gizmoAxisDirectionOnScreen(const Camera& camera, const glm::vec3& worldAxis,
                                 float radius) {
  const glm::vec3 axis = glm::normalize(worldAxis);
  const float screenX = glm::dot(axis, glm::normalize(camera.getRight()));
  const float screenY = glm::dot(axis, glm::normalize(camera.getUp()));
  return ImVec2(screenX * radius, -screenY * radius);
}

float gizmoAxisDepth(const Camera& camera, const glm::vec3& worldAxis) {
  return glm::dot(glm::normalize(worldAxis), glm::normalize(camera.getForward()));
}

void drawArrowHead(ImDrawList* drawList, const ImVec2& base, const ImVec2& tip,
                   ImU32 color, float headSize, float thickness) {
  const ImVec2 direction = ImVec2(tip.x - base.x, tip.y - base.y);
  const float lengthSq = direction.x * direction.x + direction.y * direction.y;
  if (lengthSq <= 0.0001f) {
    return;
  }

  const float length = std::sqrt(lengthSq);
  const ImVec2 dirNorm = ImVec2(direction.x / length, direction.y / length);
  const ImVec2 side = ImVec2(-dirNorm.y, dirNorm.x);
  const ImVec2 wingA =
      ImVec2(tip.x - dirNorm.x * headSize + side.x * (headSize * 0.55f),
             tip.y - dirNorm.y * headSize + side.y * (headSize * 0.55f));
  const ImVec2 wingB =
      ImVec2(tip.x - dirNorm.x * headSize - side.x * (headSize * 0.55f),
             tip.y - dirNorm.y * headSize - side.y * (headSize * 0.55f));
  drawList->AddLine(tip, wingA, color, thickness);
  drawList->AddLine(tip, wingB, color, thickness);
}

void drawOrientationGizmoOverlay(const Player& player) {
  if (!gDeveloperViewConfig.orientationGizmoEnabled) {
    return;
  }

  ImDrawList* drawList = ImGui::GetForegroundDrawList();
  const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
  const float radius = gDeveloperViewConfig.orientationGizmoRadius;
  const float margin = gDeveloperViewConfig.orientationGizmoMargin;
  const ImVec2 center(displaySize.x - radius - margin,
                      displaySize.y - radius - margin);

  drawList->AddCircleFilled(center, radius + 12.0f, IM_COL32(8, 12, 18, 180), 48);
  drawList->AddCircle(center, radius + 12.0f, IM_COL32(190, 210, 230, 90), 48, 1.0f);
  drawList->AddCircle(center, 3.0f, IM_COL32(255, 255, 255, 220), 16, 2.0f);

  std::array<GizmoAxis, 3> axes = {{
      {"X", glm::vec3(1.0f, 0.0f, 0.0f), IM_COL32(255, 96, 96, 255)},
      {"Y", glm::vec3(0.0f, 1.0f, 0.0f), IM_COL32(96, 255, 128, 255)},
      {"Z", glm::vec3(0.0f, 0.0f, 1.0f), IM_COL32(96, 170, 255, 255)},
  }};

  for (GizmoAxis& axis : axes) {
    axis.depth = gizmoAxisDepth(player.camera, axis.worldDirection);
    axis.screenDirection =
        gizmoAxisDirectionOnScreen(player.camera, axis.worldDirection, radius);
  }

  std::sort(axes.begin(), axes.end(), [](const GizmoAxis& a, const GizmoAxis& b) {
    return a.depth < b.depth;
  });

  for (const GizmoAxis& axis : axes) {
    const ImVec2 tip(center.x + axis.screenDirection.x,
                     center.y + axis.screenDirection.y);
    const float thickness = axis.depth >= 0.0f ? 2.5f : 1.5f;
    const float alphaMultiplier = axis.depth >= 0.0f ? 1.0f : 0.55f;
    const ImU32 lineColor = ImGui::GetColorU32(
        ImVec4(((axis.color >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f,
               ((axis.color >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f,
               ((axis.color >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f,
               alphaMultiplier));

    drawList->AddLine(center, tip, lineColor, thickness);
    drawArrowHead(drawList, center, tip, lineColor, 8.0f, thickness);

    const ImVec2 labelOffset =
        ImVec2(axis.screenDirection.x * 0.10f, axis.screenDirection.y * 0.10f);
    const ImVec2 labelPos(tip.x + labelOffset.x - 4.0f, tip.y + labelOffset.y - 8.0f);
    drawList->AddText(labelPos, lineColor, axis.label);
  }
}

void drawDeveloperViewWindow(const WorldStack& worldStack) {
  ImGui::SetNextWindowPos(ImVec2(24.0f, 24.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(320.0f, 150.0f), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin("Debug View")) {
    ImGui::End();
    return;
  }

  ImGui::TextUnformatted("Small runtime overlays and camera helpers.");
  ImGui::Checkbox("Orientation Gizmo", &gDeveloperViewConfig.orientationGizmoEnabled);
  ImGui::Checkbox("Enemy Head Trigger", &EnemyDebugView::headTriggerWireframeEnabled);
  ImGui::DragFloat("Gizmo Radius", &gDeveloperViewConfig.orientationGizmoRadius,
                   0.25f, 20.0f, 120.0f, "%.1f px");
  ImGui::DragFloat("Gizmo Margin", &gDeveloperViewConfig.orientationGizmoMargin,
                   0.25f, 8.0f, 96.0f, "%.1f px");
  ImGui::Separator();
  ImGui::TextUnformatted("Enemy Spawn");
  if (ImGui::Button("Spawn Enemy Now")) {
    requestImmediateEnemySpawnDebug();
    std::printf("[Enemy Spawn] Debug spawn requested.\n");
  }
  ImGui::TextUnformatted("Only works when no enemy is currently active.");
  ImGui::Text("Natural Spawn In: %s", enemySpawnTimerText(worldStack).c_str());
  ImGui::Separator();
  ImGui::TextUnformatted("World axes:");
  ImGui::BulletText("X = Red");
  ImGui::BulletText("Y = Green");
  ImGui::BulletText("Z = Blue");

  ImGui::End();
}

bool easingCombo(const char* label, EasingType& current) {
  int index = static_cast<int>(current);
  bool changed = false;
  if (ImGui::BeginCombo(label, getEasingName(current))) {
    for (int i = 0; i < kEasingTypeCount; ++i) {
      const EasingType e = static_cast<EasingType>(i);
      const bool selected = (i == index);
      if (ImGui::Selectable(getEasingName(e), selected)) {
        current = e;
        changed = true;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
  return changed;
}

void drawHandAnimationDebugWindow() {
  ImGui::SetNextWindowPos(ImVec2(360.0f, 24.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(360.0f, 560.0f), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin("Hand Animation")) {
    ImGui::End();
    return;
  }

  auto& config = HandAnimation::config;
  const auto& state = HandAnimation::state;

  const char* animName = "None";
  if (state.activeAnimation == HandAnimationState::AnimationType::SWING) {
    animName = "Swing (Break)";
  } else if (state.activeAnimation == HandAnimationState::AnimationType::PUNCH) {
    animName = "Punch (Place)";
  }
  ImGui::Text("Active: %s  Progress: %.2f", animName, state.progress);
  ImGui::ProgressBar(
      state.progress >= 0.0f ? state.progress * 0.5f : 0.0f,
      ImVec2(-1.0f, 0.0f));
  if (state.isPlaceHolding()) {
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                       "Place hold: %.3fs", state.placeHoldTimer);
  }

  ImGui::Separator();
  if (ImGui::CollapsingHeader("Break (Swing)", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::DragFloat("Speed##swing", &config.swingSpeed, 0.5f, 1.0f, 40.0f, "%.1f");
    ImGui::DragFloat("Intensity##swing", &config.swingIntensity,
                     0.05f, 0.0f, 5.0f, "%.2f");
    ImGui::Text("Rotation (Pitch / Yaw / Roll):");
    ImGui::DragFloat("Pitch##swingR", &config.swingRotationDegrees.x,
                     0.5f, -180.0f, 180.0f, "%.1f deg");
    ImGui::DragFloat("Yaw##swingR", &config.swingRotationDegrees.y,
                     0.5f, -180.0f, 180.0f, "%.1f deg");
    ImGui::DragFloat("Roll##swingR", &config.swingRotationDegrees.z,
                     0.5f, -180.0f, 180.0f, "%.1f deg");
    ImGui::Text("Translation (Fwd / Right / Up):");
    ImGui::DragFloat("Forward##swingT", &config.swingTranslation.x,
                     0.005f, -0.5f, 0.5f, "%.3f");
    ImGui::DragFloat("Right##swingT", &config.swingTranslation.y,
                     0.005f, -0.5f, 0.5f, "%.3f");
    ImGui::DragFloat("Up##swingT", &config.swingTranslation.z,
                     0.005f, -0.5f, 0.5f, "%.3f");
    easingCombo("Ease In##swing", config.swingEaseIn);
    easingCombo("Ease Out##swing", config.swingEaseOut);
  }

  if (ImGui::CollapsingHeader("Place (Punch)", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::DragFloat("Speed##punch", &config.punchSpeed, 0.5f, 1.0f, 40.0f, "%.1f");
    ImGui::DragFloat("Intensity##punch", &config.punchIntensity,
                     0.05f, 0.0f, 5.0f, "%.2f");
    ImGui::Text("Rotation (Pitch / Yaw / Roll):");
    ImGui::DragFloat("Pitch##punchR", &config.punchRotationDegrees.x,
                     0.5f, -180.0f, 180.0f, "%.1f deg");
    ImGui::DragFloat("Yaw##punchR", &config.punchRotationDegrees.y,
                     0.5f, -180.0f, 180.0f, "%.1f deg");
    ImGui::DragFloat("Roll##punchR", &config.punchRotationDegrees.z,
                     0.5f, -180.0f, 180.0f, "%.1f deg");
    ImGui::Text("Translation (Fwd / Right / Up):");
    ImGui::DragFloat("Forward##punchT", &config.punchTranslation.x,
                     0.005f, -0.5f, 0.5f, "%.3f");
    ImGui::DragFloat("Right##punchT", &config.punchTranslation.y,
                     0.005f, -0.5f, 0.5f, "%.3f");
    ImGui::DragFloat("Up##punchT", &config.punchTranslation.z,
                     0.005f, -0.5f, 0.5f, "%.3f");
    ImGui::DragFloat("Place Hold Delay", &config.placeHoldDelay,
                     0.01f, 0.0f, 1.0f, "%.3f s");
    easingCombo("Ease In##punch", config.punchEaseIn);
    easingCombo("Ease Out##punch", config.punchEaseOut);
  }

  if (ImGui::CollapsingHeader("Idle Sway")) {
    ImGui::DragFloat("Sway Speed", &config.idleSwaySpeed,
                     0.1f, 0.0f, 10.0f, "%.1f");
    ImGui::DragFloat("Sway Amount", &config.idleSwayAmount,
                     0.001f, 0.0f, 0.1f, "%.3f");
  }

  ImGui::Separator();
  if (ImGui::Button("Test Swing")) {
    HandAnimation::state.triggerSwing(config.swingSpeed);
  }
  ImGui::SameLine();
  if (ImGui::Button("Test Punch")) {
    HandAnimation::state.triggerPunch(config.punchSpeed, config.placeHoldDelay);
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset Defaults")) {
    config = HandAnimationConfig{};
  }

  ImGui::End();
}

}  // namespace

bool shouldRenderDeveloperUi() {
  return Input::hasUiFocus() || gDeveloperViewConfig.orientationGizmoEnabled;
}

void drawDeveloperUi(const WorldStack& worldStack, const Player& player) {
  if (Input::hasUiFocus()) {
    drawDeveloperViewWindow(worldStack);
    drawHandAnimationDebugWindow();
  }

  drawOrientationGizmoOverlay(player);
}

void handleGlobalShortcuts(hudPortalInfo* portalInfo,
                           hudPortalTracker* portalTracker,
                           WorldStack& worldStack,
                           Player& player, GameAudioController& audioController,
                           RuntimeUiState& uiState,
                           GameSettings& appliedSettings,
                           GameSettings& pendingSettings) {
  auto& inputActions = InputMapping::InputActionSystem::instance();

  if (player.transition != PlayerTransition::NONE && player.isInventoryOpen()) {
    player.closeInventoryForTransition();
  }

#if defined(VP_ENABLE_DEV_TOOLS)
  if (Input::keyPressed(GLFW_KEY_F9)) {
    Input::toggleFocusMode();
    std::printf("[Input] Focus: %s\n",
                Input::hasUiFocus() ? "ImGui/UI" : "Gameplay");
  }
#endif

  if (uiState.controlsCaptureOpen) {
    if (Input::keyPressed(GLFW_KEY_ESCAPE)) {
      uiState.controlsCaptureOpen = false;
      uiState.controlsCaptureIgnoreMouseLeft = false;
      uiState.controlsCaptureActionId.clear();
      uiState.controlsWarningMessage.clear();
      return;
    }

    if (uiState.controlsCaptureIgnoreMouseLeft &&
        !Input::mouseDown(GLFW_MOUSE_BUTTON_LEFT)) {
      uiState.controlsCaptureIgnoreMouseLeft = false;
    }

    InputMapping::InputBinding capturedBinding;
    if (!inputActions.tryCaptureBinding(capturedBinding,
                                        uiState.controlsCaptureIgnoreMouseLeft)) {
      return;
    }

    const InputMapping::InputActionDefinition* action =
        inputActions.findAction(uiState.controlsCaptureActionId);
    if (!action) {
      uiState.controlsCaptureOpen = false;
      uiState.controlsCaptureIgnoreMouseLeft = false;
      uiState.controlsCaptureActionId.clear();
      return;
    }

    if (!inputActions.isBindingCompatible(action->id, capturedBinding)) {
      uiState.controlsWarningMessage =
          action->inputType == InputMapping::InputBindingType::MouseButton
              ? "This action requires a mouse button."
              : "This action requires a keyboard key.";
      return;
    }

    InputMapping::ConflictInfo conflict;
    if (!inputActions.trySetBinding(action->id, capturedBinding,
                                    pendingSettings.controlOverrides, &conflict)) {
      uiState.controlsWarningMessage =
          "Binding conflict with \"" + conflict.actionLabel + "\".";
      return;
    }

    uiState.controlsCaptureOpen = false;
    uiState.controlsCaptureIgnoreMouseLeft = false;
    uiState.controlsCaptureActionId.clear();
    uiState.controlsWarningMessage.clear();
    return;
  }

  if (Input::hasUiFocus()) {
    return;
  }

  const bool cancelPressed = inputActions.wasPressed(InputActionIds::kUiCancel);
  const bool togglePausePressed =
      inputActions.wasPressed(InputActionIds::kTogglePause);
  if (cancelPressed || togglePausePressed) {
    if (portalTracker && portalTracker->isMenuOpen()) {
      portalTracker->closeMenu();
      return;
    }
    if (uiState.settingsDiscardConfirmOpen) {
      uiState.settingsDiscardConfirmOpen = false;
      return;
    }
    if (uiState.settingsMenuOpen) {
      Detail::requestCloseSettingsMenu(uiState, appliedSettings, pendingSettings);
      return;
    }
    if (player.isInventoryOpen()) {
      player.tryCloseInventory();
      return;
    }
    const bool pausedBefore = ENGINE::ISPAUSED();
    Detail::togglePauseOrCancelPortalEdit(portalInfo);
    if (pausedBefore != ENGINE::ISPAUSED()) {
      audioController.onPauseMenuToggled(ENGINE::ISPAUSED());
    }
  }

  if (inputActions.wasPressed(InputActionIds::kTogglePortalTracker) &&
      !ENGINE::ISPAUSED() &&
      player.transition == PlayerTransition::NONE &&
      !player.isInventoryOpen() &&
      (!portalInfo || !portalInfo->isEditing()) &&
      portalTracker) {
    portalTracker->toggleMenu();
    return;
  }

  if (inputActions.wasPressed(InputActionIds::kToggleInventory) &&
      !ENGINE::ISPAUSED() &&
      player.transition == PlayerTransition::NONE &&
      (!portalInfo || !portalInfo->isEditing())) {
    if (player.isInventoryOpen()) {
      player.tryCloseInventory();
    } else {
      player.setInventoryOpen(true);
    }
  }

  if (inputActions.wasPressed(InputActionIds::kToggleHud)) {
    HUD::toggleVisible();
  }

  if (Input::keyPressed(GLFW_KEY_F3)) {
    uiState.debugTextVisible = !uiState.debugTextVisible;
    syncDebugHudState(uiState, appliedSettings);
    std::printf("[Debug] Debug text: %s\n",
                Detail::onOffText(uiState.debugTextVisible));
  }

  if (!uiState.settingsMenuOpen &&
      inputActions.wasPressed(InputActionIds::kToggleFullscreen)) {
    Detail::toggleFullscreen(uiState, appliedSettings);
    pendingSettings = appliedSettings;
  }

  if (Input::keyPressed(GLFW_KEY_F8)) {
    worldStack.stepRenderDistancePreset(1);
    appliedSettings.renderDistance = worldStack.getRenderDistancePreset();
    saveGameSettings(appliedSettings);
    pendingSettings = appliedSettings;
  }
}

}  // namespace RuntimeUI
