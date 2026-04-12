// runtime_ui_hotbar.cpp
// Hotbar-specific runtime HUD setup and layout helpers.

#pragma region Includes

// 1. Standard Library
#include <cmath>
#include <cstdio>
#include <string>

// 2. Third-party Libraries
// (None explicitly included here, relies on headers)

// 3. Internal Engine/Core Modules
// (None explicitly included here)

// 4. Local Project Modules
#include "runtime/ui/runtime_ui_internal.hpp"
#include "render/config/hotbar_preview_config.hpp"
#include "runtime/ui/runtime_hud_ids.hpp"

#pragma endregion

namespace RuntimeUI::Detail {

namespace {

namespace HotbarHUDLayers {

constexpr int kBackground = 0;
constexpr int kLifeBar = 1;
constexpr int kExperienceBar = 2;
constexpr int kExperienceLabel = 3;
constexpr int kExperienceLevel = 4;
constexpr int kSelection = 5;
constexpr int kPreview = 6;
constexpr int kCounts = 7;
constexpr int kPortalCooldown = 8;

}  // namespace HotbarHUDLayers

HUDLayout makePlayerLifeSliderLayout() {
    const HotbarHUDLayout& layout = HUDHotbarPreview::config.layout;
    const int barWidth = resolveHotbarBarWidth(layout);
    const int barHeight = resolveHotbarBarHeight(layout);

    return makeHUDLayout(
        HUDAnchor::BOTTOM_CENTER,
        HUDAnchor::TOP_LEFT,
        glm::vec2(
            -static_cast<float>(barWidth) * 0.5f,
            -(static_cast<float>(barHeight + layout.offset.y +
                                 layout.lifeBarGap + layout.lifeBarHeight))
        )
    );
}

HUDLayout makePlayerExperienceSliderLayout() {
    const HotbarHUDLayout& layout = HUDHotbarPreview::config.layout;
    const int barWidth = resolveHotbarBarWidth(layout);
    const int barHeight = resolveHotbarBarHeight(layout);

    return makeHUDLayout(
        HUDAnchor::BOTTOM_CENTER,
        HUDAnchor::TOP_LEFT,
        glm::vec2(
            -static_cast<float>(barWidth) * 0.5f,
            -(static_cast<float>(
                barHeight + layout.offset.y +
                layout.lifeBarGap + layout.lifeBarHeight +
                layout.experienceBarGap + layout.experienceBarHeight
            ))
        )
    );
}

HUDLayout makePlayerExperienceTextLayout(const glm::ivec2& textOffset) {
    const HotbarHUDLayout& layout = HUDHotbarPreview::config.layout;
    const int barWidth = resolveHotbarBarWidth(layout);
    const int barHeight = resolveHotbarBarHeight(layout);

    return makeHUDLayout(
        HUDAnchor::BOTTOM_CENTER,
        HUDAnchor::TOP_LEFT,
        glm::vec2(
            -static_cast<float>(barWidth) * 0.5f + static_cast<float>(textOffset.x),
            -(static_cast<float>(
                barHeight + layout.offset.y +
                layout.lifeBarGap + layout.lifeBarHeight +
                layout.experienceBarGap + layout.experienceBarHeight
            )) + static_cast<float>(textOffset.y)
        )
    );
}

HUDLayout makePortalCooldownLayout() {
    const HotbarHUDLayout& layout = HUDHotbarPreview::config.layout;
    const int barWidth = resolveHotbarBarWidth(layout);
    const int barHeight = resolveHotbarBarHeight(layout);

    const float xOffset = static_cast<float>(barWidth) * 0.5f +
        static_cast<float>(layout.portalCooldownHorizontalOffset);
    const float yOffset = -(static_cast<float>(barHeight) * 0.5f +
        static_cast<float>(layout.offset.y + layout.portalCooldownVerticalOffset));

    return makeHUDLayout(HUDAnchor::BOTTOM_CENTER, glm::vec2(xOffset, yOffset));
}

std::string formatPortalCooldownText(const Player& player) {
    if (player.isInventoryOpen()) {
        return {};
    }

    if (player.isSandboxModeEnabled()) {
        return "Portal: SANDBOX";
    }

    return "Portal: ITEM ONLY";
}

}  // namespace

// Bottom hotbar widgets: background, selection, item previews, and counts.
void addHotbarHUD(Player& player, Renderer& renderer, WorldStack& worldStack) {
    const HotbarHUDLayout& hotbarLayout = HUDHotbarPreview::config.layout;
    const HotbarHUDStyle& hotbarStyle = HUDHotbarPreview::config.style;
    const glm::ivec2 lifeBarSize = resolveHotbarLifeBarSize(hotbarLayout);
    const glm::ivec2 experienceBarSize = resolveHotbarExperienceBarSize(hotbarLayout);

    auto* lifeSlider = new hudSlider(
        makePlayerLifeSliderLayout(),
        glm::vec2(
            static_cast<float>(lifeBarSize.x),
            static_cast<float>(lifeBarSize.y)
        ),
        [&player]() {
            const int maxLifePoints = player.getMaxLifePoints();
            if (maxLifePoints <= 0) {
                return 0.0f;
            }

            return glm::clamp(
                static_cast<float>(player.getLifePoints()) /
                    static_cast<float>(maxLifePoints),
                0.0f,
                1.0f
            );
        },
        hotbarStyle.lifeBarStyle,
        hotbarLayout.lifeBarBorderThickness,
        hotbarLayout.lifeBarFillInset,
        [&player]() {
            return !player.isInventoryOpen();
        }
    );

    auto* experienceSlider = new hudSlider(
        makePlayerExperienceSliderLayout(),
        glm::vec2(
            static_cast<float>(experienceBarSize.x),
            static_cast<float>(experienceBarSize.y)
        ),
        [&player]() {
            const float maxExperiencePoints = player.getMaxExperiencePoints();
            if (maxExperiencePoints <= 0.0f) {
                return 0.0f;
            }

            return glm::clamp(
                player.getExperiencePoints() / maxExperiencePoints,
                0.0f,
                1.0f
            );
        },
        hotbarStyle.experienceBarStyle,
        hotbarLayout.experienceBarBorderThickness,
        hotbarLayout.experienceBarFillInset,
        [&player]() {
            return !player.isInventoryOpen();
        }
    );

    HUD::add(attachToHUDGroup(
        new hudHotbar(&player, HotbarVisualPart::BACKGROUND),
        RuntimeHudIds::kHotbar, HotbarHUDLayers::kBackground
    ));

    HUD::add(attachToHUDGroup(
        lifeSlider,
        RuntimeHudIds::kHotbar, HotbarHUDLayers::kLifeBar
    ));

    HUD::add(attachToHUDGroup(
        experienceSlider,
        RuntimeHudIds::kHotbar, HotbarHUDLayers::kExperienceBar
    ));

    if (auto* experienceLabel = attachToHUDGroup(
        HUD::watchText(
            [&player](std::string& out) {
                out = player.isInventoryOpen() ? std::string{} : "xp";
            },
            makePlayerExperienceTextLayout(hotbarLayout.experienceLabelTextOffset),
            hotbarLayout.experienceTextScale,
            hotbarLayout.experienceTextFontSize,
            0.0f
        ),
        RuntimeHudIds::kHotbar, HotbarHUDLayers::kExperienceLabel)) {

        experienceLabel->setColor(hotbarStyle.experienceTextColor);
    }

    if (auto* experienceLevel = attachToHUDGroup(
        HUD::watchText(
            [&player](std::string& out) {
                if (player.isInventoryOpen()) {
                    out.clear();
                    return;
                }

                out = "level: " + std::to_string(player.getExperienceLevel());
            },
            makePlayerExperienceTextLayout(hotbarLayout.experienceLevelTextOffset),
            hotbarLayout.experienceTextScale,
            hotbarLayout.experienceTextFontSize,
            0.0f
        ),
        RuntimeHudIds::kHotbar, HotbarHUDLayers::kExperienceLevel)) {

        experienceLevel->setColor(hotbarStyle.experienceTextColor);
    }

    HUD::add(attachToHUDGroup(
        new hudHotbar(&player, HotbarVisualPart::SELECTION),
        RuntimeHudIds::kHotbar, HotbarHUDLayers::kSelection
    ));

    HUD::add(attachToHUDGroup(
        new hudHotbarPreview(&renderer, &player, &worldStack),
        RuntimeHudIds::kHotbar, HotbarHUDLayers::kPreview
    ));

    HUD::add(attachToHUDGroup(
        new hudHotbar(&player, HotbarVisualPart::COUNTS),
        RuntimeHudIds::kHotbar, HotbarHUDLayers::kCounts
    ));

    if (auto* portalCooldown = attachToHUDGroup(
        HUD::watchText(
            [&player](std::string& out) {
                out = formatPortalCooldownText(player);
            },
            makePortalCooldownLayout(), glm::vec2(1.0f), 16, 0.0f
        ),
        RuntimeHudIds::kHotbar, HotbarHUDLayers::kPortalCooldown)) {

        portalCooldown->setVisualBinder(
            [&player](hudWatchText& watchText) {
                const HotbarHUDStyle& style = HUDHotbarPreview::config.style;

                if (player.isSandboxModeEnabled()) {
                    watchText.setColor(style.portalSandboxTextColor);
                    return;
                }

                watchText.setColor(style.portalCooldownTextColor);
            }
        );
        portalCooldown->setColor(HUDHotbarPreview::config.style.portalCooldownTextColor);
    }
}

}  // namespace RuntimeUI::Detail
