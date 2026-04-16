#pragma once

#include <algorithm>
#include <cmath>

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "engine/camera.hpp"
#include "engine/input.hpp"

namespace ShaderEditor {

class OrbitCameraController {
public:
  void update(Camera& camera, float dtSeconds, bool previewHovered) {
    if (autoRotateEnabled_) {
      yawRadians_ += autoRotateSpeed_ * dtSeconds;
    }

    if (previewHovered) {
      const float scroll = Input::getScroll();
      if (std::abs(scroll) > 0.001f) {
        distance_ = std::clamp(distance_ - scroll * zoomStep_, minDistance_,
                               maxDistance_);
      }
    }

    if (manualControlEnabled_ && previewHovered &&
        Input::mouseDown(GLFW_MOUSE_BUTTON_LEFT)) {
      float mouseDeltaX = 0.0f;
      float mouseDeltaY = 0.0f;
      Input::getMouseDelta(mouseDeltaX, mouseDeltaY);
      yawRadians_ -= mouseDeltaX * orbitSensitivity_;
      pitchRadians_ = std::clamp(pitchRadians_ + mouseDeltaY * orbitSensitivity_,
                                 -kMaxPitchRadians, kMaxPitchRadians);
    }

    apply(camera);
  }

  void reset(Camera& camera) {
    target_ = glm::vec3(0.5f, 0.5f, 0.5f);
    yawRadians_ = glm::radians(45.0f);
    pitchRadians_ = glm::radians(28.0f);
    distance_ = 3.6f;
    camera.baseFov = 60.0f;
    camera.nearPlane = 0.05f;
    camera.farPlane = 64.0f;
    apply(camera);
  }

  // Positions the camera to frame the 3x3 world cluster preview (blocks at
  // [0,3]x[0,1]x[0,3], center at (1.5, 0.5, 1.5)).
  void resetForWorldCluster(Camera& camera) {
    target_ = glm::vec3(1.5f, 0.5f, 1.5f);
    yawRadians_ = glm::radians(35.0f);
    pitchRadians_ = glm::radians(38.0f);
    distance_ = 8.5f;
    camera.baseFov = 60.0f;
    camera.nearPlane = 0.05f;
    camera.farPlane = 64.0f;
    apply(camera);
  }

  bool autoRotateEnabled() const { return autoRotateEnabled_; }
  void setAutoRotateEnabled(bool enabled) { autoRotateEnabled_ = enabled; }

  bool manualControlEnabled() const { return manualControlEnabled_; }
  void setManualControlEnabled(bool enabled) { manualControlEnabled_ = enabled; }

  float autoRotateSpeed() const { return autoRotateSpeed_; }
  void setAutoRotateSpeed(float speed) { autoRotateSpeed_ = std::max(0.0f, speed); }

  float distance() const { return distance_; }
  void setDistance(float distance) {
    distance_ = std::clamp(distance, minDistance_, maxDistance_);
  }

private:
  static constexpr float kMaxPitchRadians =
      glm::half_pi<float>() - 0.05f;

  glm::vec3 target_{0.5f, 0.5f, 0.5f};
  float yawRadians_ = glm::radians(45.0f);
  float pitchRadians_ = glm::radians(28.0f);
  float distance_ = 3.6f;
  float minDistance_ = 1.6f;
  float maxDistance_ = 12.0f;
  float zoomStep_ = 0.35f;
  float orbitSensitivity_ = 0.010f;
  bool autoRotateEnabled_ = true;
  bool manualControlEnabled_ = true;
  float autoRotateSpeed_ = 0.20f;

  void apply(Camera& camera) const {
    const float cosPitch = std::cos(pitchRadians_);
    const glm::vec3 offset(std::sin(yawRadians_) * cosPitch * distance_,
                           std::sin(pitchRadians_) * distance_,
                           std::cos(yawRadians_) * cosPitch * distance_);
    camera.position = target_ + offset;
    camera.lookAt(target_);
  }
};

} // namespace ShaderEditor
