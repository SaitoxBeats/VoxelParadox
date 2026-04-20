#include "hud_portal_tracker.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/engine.hpp"
#include "engine/input.hpp"
#include "hud.hpp"
#include "hud_text.hpp"
#include "input/input_action_ids.hpp"
#include "input/input_action_system.hpp"
#include "player/player.hpp"
#include "world/persistence/world_stack.hpp"

namespace {

constexpr int kMenuPadding = 18;
constexpr int kMenuTitleGap = 18;
constexpr int kMenuRowHeight = 48;
constexpr int kMenuRowGap = 8;
constexpr int kMenuVisibleRows = 8;
constexpr float kWaypointFadeSpeed = 7.5f;
constexpr float kWaypointDeactivateDistance = 2.5f;
constexpr float kWaypointWorldHeight = 1.35f;
constexpr float kWaypointEdgeMargin = 68.0f;
constexpr float kWaypointLabelGap = 18.0f;
constexpr float kWaypointMarkerSize = 8.0f;

std::string formatPortalDistance(float distanceMeters) {
    char buffer[32];
    if (distanceMeters >= 100.0f) {
        std::snprintf(buffer, sizeof(buffer), "%.0f m", distanceMeters);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.1f m", distanceMeters);
    }
    return buffer;
}

}  // namespace

hudPortalTracker::hudPortalTracker(Player* player, WorldStack* worldStack, int fontSize,
                                   const std::string& fontPath)
    : player(player), worldStack(worldStack), fontSize(fontSize), fontPath(fontPath) {
    titleText = new hudText("Tracked Portals", 0, 0, glm::vec2(1.0f), fontSize + 4, fontPath);
    rowText = new hudText("", 0, 0, glm::vec2(1.0f), fontSize, fontPath);
    detailText = new hudText("", 0, 0, glm::vec2(0.9f), fontSize - 2, fontPath);
    emptyText = new hudText("No named portals in this universe yet.", 0, 0,
                            glm::vec2(1.0f), fontSize, fontPath);
    waypointText = new hudText("", 0, 0, glm::vec2(1.0f), fontSize, fontPath);

    titleText->setColor(glm::vec3(0.96f, 0.98f, 1.0f));
    rowText->setColor(glm::vec3(0.90f, 0.95f, 1.0f));
    detailText->setColor(glm::vec3(0.70f, 0.78f, 0.90f));
    emptyText->setColor(glm::vec3(0.74f, 0.78f, 0.84f));
    waypointText->setColor(glm::vec3(0.98f, 0.96f, 0.72f));
}

hudPortalTracker::~hudPortalTracker() {
    delete titleText;
    delete rowText;
    delete detailText;
    delete emptyText;
    delete waypointText;
}

void hudPortalTracker::toggleMenu() {
    menuOpen = !menuOpen;
    if (menuOpen) {
        refreshEntries();
        clampSelection();
    }
}

void hudPortalTracker::closeMenu() {
    menuOpen = false;
    hoveredIndex = -1;
}

WorldSaveService::PlayerData::PortalTrackerState
hudPortalTracker::capturePersistentState() const {
    WorldSaveService::PlayerData::PortalTrackerState state;
    state.trackingActive = trackingActive;
    state.trackedBlock = trackedBlock;
    state.trackedWorldSeed = trackedWorldSeed;
    state.trackedWorldBiome = trackedWorldBiome;
    state.trackedChildSeed = trackedChildSeed;
    state.trackedChildBiome = trackedChildBiome;
    state.trackedUniverseName = trackedUniverseName;
    return state;
}

void hudPortalTracker::applyPersistentState(
    const WorldSaveService::PlayerData::PortalTrackerState& state) {
    if (!state.trackingActive) {
        clearTracking();
        return;
    }

    trackingActive = true;
    trackedBlock = state.trackedBlock;
    trackedWorldSeed = state.trackedWorldSeed;
    trackedWorldBiome = state.trackedWorldBiome;
    trackedChildSeed = state.trackedChildSeed;
    trackedChildBiome = state.trackedChildBiome;
    trackedUniverseName = state.trackedUniverseName;
    waypointFade = 0.0f;
    waypointDeactivateWhenHidden = false;
    waypointVisibleThisFrame = false;
    waypointOffscreen = false;
    waypointScreenPos = glm::vec2(0.0f);
    waypointLabel.clear();
    closeMenu();
}

void hudPortalTracker::refreshEntries() {
    entries.clear();
    if (!player || !worldStack) {
        selectedIndex = 0;
        scrollOffset = 0;
        return;
    }

    const std::vector<WorldStack::NamedPortalEntry> namedPortals =
        worldStack->listNamedPortalsInCurrentWorld();
    entries.reserve(namedPortals.size());
    for (const WorldStack::NamedPortalEntry& namedPortal : namedPortals) {
        MenuEntry entry;
        entry.block = namedPortal.block;
        entry.childSeed = namedPortal.childSeed;
        entry.childBiome = namedPortal.childBiome;
        entry.universeName = namedPortal.universeName;
        const glm::vec3 portalCenter =
            glm::vec3(entry.block) + glm::vec3(0.5f, kWaypointWorldHeight, 0.5f);
        entry.distanceMeters = glm::length(portalCenter - player->camera.position);
        entries.push_back(std::move(entry));
    }

    clampSelection();
}

void hudPortalTracker::clampSelection() {
    if (entries.empty()) {
        selectedIndex = 0;
        scrollOffset = 0;
        return;
    }

    selectedIndex = std::clamp(selectedIndex, 0, static_cast<int>(entries.size()) - 1);
    const int maxScroll = std::max(0, static_cast<int>(entries.size()) - kMenuVisibleRows);
    scrollOffset = std::clamp(scrollOffset, 0, maxScroll);
    if (selectedIndex < scrollOffset) {
        scrollOffset = selectedIndex;
    }
    if (selectedIndex >= scrollOffset + kMenuVisibleRows) {
        scrollOffset = selectedIndex - kMenuVisibleRows + 1;
    }
}

void hudPortalTracker::activateTracking(const MenuEntry& entry) {
    if (!worldStack || !worldStack->currentWorld()) {
        return;
    }

    FractalWorld* world = worldStack->currentWorld();
    trackingActive = true;
    trackedBlock = entry.block;
    trackedWorldSeed = world->seed;
    trackedWorldBiome = world->biomeSelection;
    trackedChildSeed = entry.childSeed;
    trackedChildBiome = entry.childBiome;
    trackedUniverseName = entry.universeName;
    waypointFade = 0.0f;
    waypointDeactivateWhenHidden = false;
    closeMenu();
}

void hudPortalTracker::clearTracking() {
    trackingActive = false;
    trackedBlock = glm::ivec3(0);
    trackedWorldSeed = 0;
    trackedWorldBiome = BiomeSelection{};
    trackedChildSeed = 0;
    trackedChildBiome = BiomeSelection{};
    trackedUniverseName.clear();
    waypointDistanceMeters = 0.0f;
    waypointFade = 0.0f;
    waypointDeactivateWhenHidden = false;
    waypointVisibleThisFrame = false;
    waypointOffscreen = false;
    waypointScreenPos = glm::vec2(0.0f);
    waypointLabel.clear();
}

bool hudPortalTracker::pointInRect(const glm::vec2& point, const glm::ivec2& rectPos,
                                   const glm::vec2& rectSize) {
    return point.x >= static_cast<float>(rectPos.x) &&
           point.x <= static_cast<float>(rectPos.x) + rectSize.x &&
           point.y >= static_cast<float>(rectPos.y) &&
           point.y <= static_cast<float>(rectPos.y) + rectSize.y;
}

void hudPortalTracker::drawRect(Shader& shader, const glm::ivec2& pos,
                                const glm::vec2& size, const glm::vec4& color) {
    if (size.x <= 0.0f || size.y <= 0.0f) {
        return;
    }

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(static_cast<float>(pos.x),
                                            static_cast<float>(pos.y), 0.0f));
    model = glm::scale(model, glm::vec3(size.x, size.y, 1.0f));

    shader.setMat4("model", model);
    shader.setInt("isText", 0);
    shader.setVec4("color", color);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, HUD::getWhiteTexture());
    HUD::bindQuad();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    HUD::unbindQuad();
}

bool hudPortalTracker::projectWaypoint(int screenWidth, int screenHeight,
                                       const glm::vec3& worldPos,
                                       glm::vec2& outScreen,
                                       bool& outOffscreen) const {
    if (!player || screenWidth <= 0 || screenHeight <= 0) {
        return false;
    }

    const glm::vec3 toTarget = worldPos - player->camera.position;
    if (glm::dot(toTarget, toTarget) <= 0.0001f) {
        outScreen = glm::vec2(screenWidth * 0.5f, screenHeight * 0.5f);
        outOffscreen = false;
        return true;
    }

    const glm::vec3 direction = glm::normalize(toTarget);
    const float forwardDot = glm::dot(direction, player->camera.getForward());

    glm::vec2 projected(0.0f);
    float ndcDepth = 0.0f;
    if (forwardDot > 0.02f &&
        player->camera.WorldToScreenPoint(worldPos, screenWidth, screenHeight, projected,
                                          &ndcDepth) &&
        ndcDepth >= -1.0f && ndcDepth <= 1.0f) {
        outScreen = projected;
        outOffscreen = false;
        return true;
    }

    glm::vec2 screenDirection(glm::dot(direction, player->camera.getRight()),
                              -glm::dot(direction, player->camera.getUp()));
    if (glm::dot(screenDirection, screenDirection) <= 0.0001f) {
        screenDirection = glm::vec2(0.0f, -1.0f);
    } else {
        screenDirection = glm::normalize(screenDirection);
    }

    const glm::vec2 center(screenWidth * 0.5f, screenHeight * 0.5f);
    const glm::vec2 bounds(screenWidth * 0.5f - kWaypointEdgeMargin,
                           screenHeight * 0.5f - kWaypointEdgeMargin);
    const float xScale = std::abs(screenDirection.x) > 0.0001f
        ? bounds.x / std::abs(screenDirection.x)
        : 1000000.0f;
    const float yScale = std::abs(screenDirection.y) > 0.0001f
        ? bounds.y / std::abs(screenDirection.y)
        : 1000000.0f;

    outScreen = center + screenDirection * std::min(xScale, yScale);
    outOffscreen = true;
    return true;
}

void hudPortalTracker::updateMenu(int screenWidth, int screenHeight) {
    auto& inputActions = InputMapping::InputActionSystem::instance();

    hoveredIndex = -1;
    if (!menuOpen) {
        return;
    }

    if (ENGINE::ISPAUSED() || player->isInventoryOpen() ||
        player->transition != PlayerTransition::NONE) {
        closeMenu();
        return;
    }

    refreshEntries();

    const int visibleRows = std::min(kMenuVisibleRows, static_cast<int>(entries.size()));
    const float titleHeight = titleText ? titleText->measure().y : 24.0f;
    const int contentRows = std::max(visibleRows, 1);
    menuSize.x = 430.0f;
    menuSize.y = static_cast<float>(kMenuPadding * 2) + titleHeight + kMenuTitleGap +
                 contentRows * kMenuRowHeight + std::max(0, contentRows - 1) * kMenuRowGap;
    menuPos = glm::ivec2(
        static_cast<int>((screenWidth - menuSize.x) * 0.5f),
        static_cast<int>((screenHeight - menuSize.y) * 0.5f));

    const float scroll = Input::getScroll();
    if (scroll > 0.1f) {
        selectedIndex = std::max(0, selectedIndex - 1);
    } else if (scroll < -0.1f) {
        selectedIndex = std::min(std::max(0, static_cast<int>(entries.size()) - 1),
                                 selectedIndex + 1);
    }

    if (inputActions.wasPressed(InputActionIds::kUiUp)) {
        selectedIndex = std::max(0, selectedIndex - 1);
    }
    if (inputActions.wasPressed(InputActionIds::kUiDown)) {
        selectedIndex = std::min(std::max(0, static_cast<int>(entries.size()) - 1),
                                 selectedIndex + 1);
    }

    clampSelection();

    float mouseX = 0.0f;
    float mouseY = 0.0f;
    Input::getMousePosFramebuffer(mouseX, mouseY, screenWidth, screenHeight);
    const glm::vec2 mousePos(mouseX, mouseY);

    const int startIndex = scrollOffset;
    const int endIndex =
        std::min(startIndex + kMenuVisibleRows, static_cast<int>(entries.size()));
    const int firstRowY = menuPos.y + kMenuPadding +
                          static_cast<int>(std::round(titleHeight)) + kMenuTitleGap;

    for (int index = startIndex; index < endIndex; index++) {
        const int rowOffset = index - startIndex;
        const glm::ivec2 rowPos(menuPos.x + kMenuPadding,
                                firstRowY + rowOffset * (kMenuRowHeight + kMenuRowGap));
        const glm::vec2 rowSize(menuSize.x - static_cast<float>(kMenuPadding * 2),
                                static_cast<float>(kMenuRowHeight));
        if (pointInRect(mousePos, rowPos, rowSize)) {
            hoveredIndex = index;
            selectedIndex = index;
            break;
        }
    }

    clampSelection();

    const bool pressedEnter =
        inputActions.wasPressed(InputActionIds::kUiAccept);
    const bool clickedRow =
        hoveredIndex >= 0 && Input::mousePressed(GLFW_MOUSE_BUTTON_LEFT);
    if (pressedEnter && !entries.empty()) {
        activateTracking(entries[static_cast<std::size_t>(selectedIndex)]);
    } else if (clickedRow) {
        activateTracking(entries[static_cast<std::size_t>(hoveredIndex)]);
    }
}

void hudPortalTracker::updateWaypoint(int screenWidth, int screenHeight) {
    waypointVisibleThisFrame = false;

    const float dt = glm::min(static_cast<float>(ENGINE::GETDELTATIME()), 0.1f);

    if (!trackingActive || !player || !worldStack || !worldStack->currentWorld()) {
        waypointFade = glm::max(0.0f, waypointFade - dt * kWaypointFadeSpeed);
        if (waypointFade <= 0.0f) {
            clearTracking();
        }
        return;
    }

    FractalWorld* world = worldStack->currentWorld();
    if (world->seed != trackedWorldSeed || world->biomeSelection != trackedWorldBiome) {
        waypointDeactivateWhenHidden = true;
    } else {
        // Use the persisted portal registry so tracking survives chunk unloads.
        const auto portalIt = world->portalBlocks.find(trackedBlock);
        if (portalIt == world->portalBlocks.end() || portalIt->second != trackedChildSeed) {
            waypointDeactivateWhenHidden = true;
        } else {
            trackedChildBiome =
                worldStack->getResolvedPortalBiomeSelection(*world, trackedBlock, trackedChildSeed);
            trackedUniverseName = worldStack->getUniverseName(trackedChildSeed, trackedChildBiome);
            if (trackedUniverseName.empty()) {
                waypointDeactivateWhenHidden = true;
            } else {
                const glm::vec3 worldPos =
                    glm::vec3(trackedBlock) + glm::vec3(0.5f, kWaypointWorldHeight, 0.5f);
                waypointDistanceMeters = glm::length(worldPos - player->camera.position);
                if (waypointDistanceMeters <= kWaypointDeactivateDistance) {
                    waypointDeactivateWhenHidden = true;
                } else {
                    waypointVisibleThisFrame =
                        projectWaypoint(screenWidth, screenHeight, worldPos,
                                        waypointScreenPos, waypointOffscreen);
                    waypointLabel =
                        trackedUniverseName + "\n" + formatPortalDistance(waypointDistanceMeters);
                    waypointDeactivateWhenHidden = false;
                }
            }
        }
    }

    const float targetFade =
        waypointVisibleThisFrame && !waypointDeactivateWhenHidden ? 1.0f : 0.0f;
    if (waypointFade < targetFade) {
        waypointFade = glm::min(targetFade, waypointFade + dt * kWaypointFadeSpeed);
    } else {
        waypointFade = glm::max(targetFade, waypointFade - dt * kWaypointFadeSpeed);
    }

    if (waypointDeactivateWhenHidden && waypointFade <= 0.0f) {
        clearTracking();
    }
}

void hudPortalTracker::update(int screenWidth, int screenHeight) {
    if (!player || !worldStack) {
        closeMenu();
        clearTracking();
        return;
    }

    updateMenu(screenWidth, screenHeight);
    updateWaypoint(screenWidth, screenHeight);
}

void hudPortalTracker::draw(Shader& shader, int screenWidth, int screenHeight) {
    if (menuOpen) {
        const float titleHeight = titleText ? titleText->measure().y : 24.0f;
        drawRect(shader, menuPos, menuSize, glm::vec4(0.02f, 0.03f, 0.05f, 0.88f));
        drawRect(shader, menuPos - glm::ivec2(2), menuSize + glm::vec2(4.0f),
                 glm::vec4(0.75f, 0.82f, 0.92f, 0.16f));

        if (titleText) {
            titleText->setPosition(menuPos.x + kMenuPadding, menuPos.y + kMenuPadding);
            titleText->draw(shader, screenWidth, screenHeight);
        }

        const int firstRowY = menuPos.y + kMenuPadding +
                              static_cast<int>(std::round(titleHeight)) + kMenuTitleGap;
        if (entries.empty()) {
            if (emptyText) {
                emptyText->setPosition(menuPos.x + kMenuPadding, firstRowY + 6);
                emptyText->draw(shader, screenWidth, screenHeight);
            }
        } else {
            const int startIndex = scrollOffset;
            const int endIndex =
                std::min(startIndex + kMenuVisibleRows, static_cast<int>(entries.size()));
            for (int index = startIndex; index < endIndex; index++) {
                const MenuEntry& entry = entries[static_cast<std::size_t>(index)];
                const int rowOffset = index - startIndex;
                const glm::ivec2 rowPos(
                    menuPos.x + kMenuPadding,
                    firstRowY + rowOffset * (kMenuRowHeight + kMenuRowGap));
                const glm::vec2 rowSize(menuSize.x - static_cast<float>(kMenuPadding * 2),
                                        static_cast<float>(kMenuRowHeight));

                const bool selected = index == selectedIndex;
                const bool hovered = index == hoveredIndex;
                const glm::vec4 rowColor = hovered
                    ? glm::vec4(0.22f, 0.32f, 0.46f, 0.88f)
                    : selected
                        ? glm::vec4(0.14f, 0.22f, 0.34f, 0.88f)
                        : glm::vec4(0.07f, 0.09f, 0.14f, 0.68f);
                drawRect(shader, rowPos, rowSize, rowColor);

                if (rowText) {
                    rowText->setText(entry.universeName);
                    rowText->setColor(selected ? glm::vec3(1.0f, 0.97f, 0.80f)
                                               : glm::vec3(0.90f, 0.95f, 1.0f));
                    rowText->setPosition(rowPos.x + 12, rowPos.y + 8);
                    rowText->draw(shader, screenWidth, screenHeight);
                }

                if (detailText) {
                    detailText->setText(entry.childBiome.displayName + "  |  " +
                                        formatPortalDistance(entry.distanceMeters));
                    detailText->setColor(glm::vec3(0.70f, 0.78f, 0.90f));
                    detailText->setPosition(rowPos.x + 12, rowPos.y + 28);
                    detailText->draw(shader, screenWidth, screenHeight);
                }
            }
        }
    }

    if (waypointFade <= 0.0f || !waypointText || waypointLabel.empty()) {
        return;
    }

    waypointText->setText(waypointLabel);
    waypointText->setOpacity(waypointFade);
    waypointText->setColor(waypointOffscreen ? glm::vec3(0.90f, 0.94f, 1.0f)
                                             : glm::vec3(0.98f, 0.96f, 0.72f));

    const glm::vec2 textSize = waypointText->measure();
    glm::ivec2 textPos(
        static_cast<int>(std::round(waypointScreenPos.x - textSize.x * 0.5f)),
        static_cast<int>(std::round(waypointScreenPos.y - textSize.y - kWaypointLabelGap)));
    textPos.x = std::clamp(textPos.x, 12, std::max(12, screenWidth - static_cast<int>(textSize.x) - 12));
    textPos.y = std::clamp(textPos.y, 12, std::max(12, screenHeight - static_cast<int>(textSize.y) - 12));

    const glm::ivec2 panelPos(textPos.x - 10, textPos.y - 8);
    const glm::vec2 panelSize(textSize.x + 20.0f, textSize.y + 16.0f);
    drawRect(shader, panelPos, panelSize,
             glm::vec4(0.03f, 0.05f, 0.08f, 0.78f * waypointFade));

    waypointText->setPosition(textPos.x, textPos.y);
    waypointText->draw(shader, screenWidth, screenHeight);

    const glm::ivec2 markerPos(
        static_cast<int>(std::round(waypointScreenPos.x - kWaypointMarkerSize * 0.5f)),
        static_cast<int>(std::round(waypointScreenPos.y - kWaypointMarkerSize * 0.5f)));
    drawRect(shader, markerPos, glm::vec2(kWaypointMarkerSize),
             glm::vec4(0.98f, 0.96f, 0.72f, waypointFade));
}
