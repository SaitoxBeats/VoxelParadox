// runtime_app_screenshot.cpp
// Gameplay screenshot helpers extracted from runtime_app_loop.cpp.

// 1. Standard Library
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <vector>

// 2. External Libraries
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

// 3. Project Headers
#include "runtime/app/runtime_app_screenshot.hpp"

#include "engine/engine.hpp"
#include "engine/input.hpp"
#include "path/app_paths.hpp"
#include "player/player.hpp"
#include "render/hud/hud_portal_info.hpp"
#include "render/hud/hud_portal_tracker.hpp"
#include "runtime/state/game_chat.hpp"

namespace {

    std::filesystem::path makeScreenshotPath() {
        const std::filesystem::path screenshotDir = AppPaths::screenshotsRoot();
        std::error_code ec;
        std::filesystem::create_directories(screenshotDir, ec);

        if (ec) {
            return {};
        }

        const auto now = std::chrono::system_clock::now();
        const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
        std::tm localTime{};
        localtime_s(&localTime, &nowTime);

        std::ostringstream filename;
        filename << "screenshot_" << std::put_time(&localTime, "%Y%m%d_%H%M%S");

        const auto millis =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()
            ) % 1000;
        filename << "_" << std::setw(3) << std::setfill('0')
                 << millis.count() << ".png";

        return screenshotDir / filename.str();
    }

}  // namespace

namespace RuntimeAppInternal {

    bool canCaptureGameplayScreenshot(const Player& player, const GameChat& gameChat,
                                      hudPortalInfo* portalInfo,
                                      hudPortalTracker* portalTracker) {
        return !ENGINE::ISPAUSED() &&
            !Input::hasUiFocus() &&
            !player.isInventoryOpen() &&
            !gameChat.isOpen() &&
            player.transition == PlayerTransition::NONE &&
            (!portalInfo || !portalInfo->isEditing()) &&
            (!portalTracker || !portalTracker->isMenuOpen());
    }

    bool captureGameplayScreenshot(GLFWwindow* window, GameChat* gameChat) {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);

        if (width <= 0 || height <= 0) {
            return false;
        }

        const std::filesystem::path screenshotPath = makeScreenshotPath();
        if (screenshotPath.empty()) {
            return false;
        }

        std::vector<unsigned char> pixels(
            static_cast<std::size_t>(width) *
            static_cast<std::size_t>(height) * 4u
        );

        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadBuffer(GL_BACK);
        glReadPixels(
            0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data()
        );

        // OpenGL reads bottom-to-top. Flip rows so the PNG matches the on-screen view.
        const std::size_t rowBytes = static_cast<std::size_t>(width) * 4u;
        std::vector<unsigned char> flipped(pixels.size());

        for (int y = 0; y < height; ++y) {
            const std::size_t srcOffset =
                static_cast<std::size_t>(height - 1 - y) * rowBytes;
            const std::size_t dstOffset = static_cast<std::size_t>(y) * rowBytes;
            std::copy_n(pixels.data() + srcOffset, rowBytes, flipped.data() + dstOffset);
        }

        const int writeOk = stbi_write_png(
            screenshotPath.string().c_str(),
            width,
            height,
            4,
            flipped.data(),
            width * 4
        );

        if (writeOk == 0) {
            return false;
        }

        std::printf("[Screenshot] Saved to %s\n", screenshotPath.string().c_str());

        if (gameChat) {
            gameChat->pushNotification(
                "Screenshot saved to " + screenshotPath.string()
            );
        }

        return true;
    }

}  // namespace RuntimeAppInternal
