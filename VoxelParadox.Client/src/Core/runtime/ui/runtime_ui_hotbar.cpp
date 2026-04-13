// runtime_ui_hotbar.cpp
// Unity mental model: Hotbar-specific runtime HUD setup and layout helpers.
// Assembles the visual layers for the hotbar, including life, XP, and items.

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

#pragma region 1. Constants & Enums
        // --- 1. Constants & Enums ---

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

#pragma endregion

#pragma region 2. Layout Builders
// --- 2. Layout Builders ---

        HUDLayout makePlayerLifeSliderLayout() {
            const HotbarHUDLayout& layout = HUDHotbarPreview::config.layout;
            const int barWidth = resolveHotbarBarWidth(layout);
            const int barHeight = resolveHotbarBarHeight(layout);

            return makeHUDLayout(
                HUDAnchor::BOTTOM_CENTER,
                HUDAnchor::TOP_LEFT,
                glm::vec2(
                    -static_cast<float>(barWidth) * 0.5f,
                    -(static_cast<float>(barHeight + layout.offset.y + layout.lifeBarGap + layout.lifeBarHeight))
                )
            );
        }

        HUDLayout makePlayerLifeTextLayout(const glm::ivec2& textOffset) {
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
                        layout.lifeBarGap + layout.lifeBarHeight
                        )) + static_cast<float>(textOffset.y)
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

            const float xOffset = static_cast<float>(barWidth) * 0.5f + static_cast<float>(layout.portalCooldownHorizontalOffset);
            const float yOffset = -(static_cast<float>(barHeight) * 0.5f + static_cast<float>(layout.offset.y + layout.portalCooldownVerticalOffset));

            return makeHUDLayout(HUDAnchor::BOTTOM_CENTER, glm::vec2(xOffset, yOffset));
        }

#pragma endregion

#pragma region 3. Text Formatters
        // --- 3. Text Formatters ---

        std::string formatPortalCooldownText(const Player& player) {
            if (player.isInventoryOpen()) {
                return {};
            }

            if (player.isSandboxModeEnabled()) {
                return "SANDBOX";
            }

            return "";
        }

#pragma endregion

    }  // namespace

#pragma region 4. Core HUD Construction
// --- 4. Core HUD Construction ---

// Bottom hotbar widgets: background, selection, item previews, and counts.
    void addHotbarHUD(Player& player, Renderer& renderer, WorldStack& worldStack) {
        const HotbarHUDLayout& hotbarLayout = HUDHotbarPreview::config.layout;
        const HotbarHUDStyle& hotbarStyle = HUDHotbarPreview::config.style;

        const glm::ivec2 lifeBarSize = resolveHotbarLifeBarSize(hotbarLayout);
        const glm::ivec2 experienceBarSize = resolveHotbarExperienceBarSize(hotbarLayout);

        // --- 1. Sliders (Life & XP) ---
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
                    static_cast<float>(player.getLifePoints()) / static_cast<float>(maxLifePoints),
                    0.0f, 1.0f
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
                    0.0f, 1.0f
                );
            },
            hotbarStyle.experienceBarStyle,
            hotbarLayout.experienceBarBorderThickness,
            hotbarLayout.experienceBarFillInset,
            [&player]() {
                return !player.isInventoryOpen();
            }
        );

        // --- 2. Attach Core Background & Bars ---
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

        // --- 3. Life & XP Labels ---
        if (auto* healthLabel = attachToHUDGroup(
            HUD::watchText(
                [&player](std::string& out) {
                    out = player.isInventoryOpen() ? std::string{} : "health";
                },
                makePlayerLifeTextLayout(hotbarLayout.lifeLabelTextOffset),
                hotbarLayout.experienceTextScale,
                hotbarLayout.experienceTextFontSize,
                0.0f
            ),
            RuntimeHudIds::kHotbar, HotbarHUDLayers::kExperienceLabel)) {

            healthLabel->setVisualBinder(
                [&player](hudWatchText& watchText) {
                    watchText.setColor(player.getLifeTextColor());
                }
            );
            healthLabel->setColor(player.getLifeTextColor());
        }

        if (auto* experienceLabel = attachToHUDGroup(
            HUD::watchText(
                [&player](std::string& out) {
                    out = player.isInventoryOpen() ? std::string{} : "AXÉ";
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

        // --- 4. Inventory/Item Logic Overlays ---
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

        // --- 5. Portal Cooldown Text ---
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

#pragma endregion

}  // namespace RuntimeUI::Detail
