// runtime_app_screenshot.hpp
// Gameplay screenshot helpers extracted from runtime_app_loop.cpp.

#pragma once

struct GLFWwindow;

class GameChat;
class Player;
class hudPortalInfo;
class hudPortalTracker;

namespace RuntimeAppInternal {

bool canCaptureGameplayScreenshot(const Player& player, const GameChat& gameChat,
                                  hudPortalInfo* portalInfo,
                                  hudPortalTracker* portalTracker);

bool captureGameplayScreenshot(GLFWwindow* window);

}  // namespace RuntimeAppInternal
