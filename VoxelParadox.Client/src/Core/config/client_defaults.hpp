#pragma once

// Central place for client-side defaults that were previously spread across
// runtime, settings, and world bootstrap code.

#include <array>
#include <cstdint>

#include <glm/glm.hpp>

namespace ClientDefaults {

inline constexpr int kOpenGLMajor = 4;
inline constexpr int kOpenGLMinor = 6;
inline constexpr std::uint32_t kRootSeed = 42;

inline constexpr char kWindowTitle[] = "Voxel Paradox";
inline constexpr char kPreferredRootBiomePresetId[] = "waves_of_god";
inline constexpr char kDefaultFontFile[] = "zpix.ttf";

inline constexpr glm::vec3 kPlayerSpawnPosition(0.0f);
inline constexpr glm::ivec2 kDefaultWindowedResolution(1280, 720);

inline constexpr float kMinMouseSensitivity = 0.0005f;
inline constexpr float kMaxMouseSensitivity = 0.02f;
inline constexpr float kMouseSensitivityStep = 0.00025f;
inline constexpr float kDefaultFieldOfView = 75.0f;
inline constexpr float kMinFieldOfView = 30.0f;
inline constexpr float kMaxFieldOfView = 120.0f;
inline constexpr float kFieldOfViewStep = 5.0f;
inline constexpr float kDefaultRenderScale = 0.30f;
inline constexpr float kMinRenderScale = 0.25f;
inline constexpr float kMaxRenderScale = 1.0f;
inline constexpr float kRenderScaleStep = 0.05f;
inline constexpr int kDefaultAntiAliasingSamples = 0;
inline constexpr std::array<int, 4> kAntiAliasingSampleOptions{0, 2, 4, 8};

inline constexpr float kDebugHudLineHeight = 22.0f;
inline constexpr float kDebugHudMargin = 10.0f;
inline constexpr double kDebugHudUpdateIntervalSeconds = 0.15;

} // namespace ClientDefaults
