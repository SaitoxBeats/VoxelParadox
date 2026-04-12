// Tracks named portals in the current world and renders the portal tracker menu
// plus an on-screen waypoint for the selected portal.

#pragma once

#include "hud_element.hpp"
#include "world/persistence/world_save_service.hpp"
#include "world/biome/biome.hpp"

#include <cstdint>
#include <string>
#include <vector>

class Player;
class WorldStack;
class hudText;

class hudPortalTracker : public HUDElement {
public:
    hudPortalTracker(Player* player, WorldStack* worldStack, int fontSize = 18,
                     const std::string& fontPath = "");
    ~hudPortalTracker() override;

    void update(int screenWidth, int screenHeight) override;
    void draw(class Shader& shader, int screenWidth, int screenHeight) override;

    bool isMenuOpen() const { return menuOpen; }
    void toggleMenu();
    void closeMenu();
    WorldSaveService::PlayerData::PortalTrackerState capturePersistentState() const;
    void applyPersistentState(
        const WorldSaveService::PlayerData::PortalTrackerState& state);

private:
    struct MenuEntry {
        glm::ivec3 block{0};
        std::uint32_t childSeed = 0;
        BiomeSelection childBiome{};
        std::string universeName;
        float distanceMeters = 0.0f;
    };

    Player* player = nullptr;
    WorldStack* worldStack = nullptr;
    hudText* titleText = nullptr;
    hudText* rowText = nullptr;
    hudText* detailText = nullptr;
    hudText* emptyText = nullptr;
    hudText* waypointText = nullptr;
    int fontSize = 18;
    std::string fontPath;

    bool menuOpen = false;
    std::vector<MenuEntry> entries;
    int selectedIndex = 0;
    int scrollOffset = 0;
    int hoveredIndex = -1;
    glm::ivec2 menuPos{0};
    glm::vec2 menuSize{0.0f};

    bool trackingActive = false;
    glm::ivec3 trackedBlock{0};
    std::uint32_t trackedWorldSeed = 0;
    BiomeSelection trackedWorldBiome{};
    std::uint32_t trackedChildSeed = 0;
    BiomeSelection trackedChildBiome{};
    std::string trackedUniverseName;
    float waypointDistanceMeters = 0.0f;
    float waypointFade = 0.0f;
    bool waypointDeactivateWhenHidden = false;
    bool waypointVisibleThisFrame = false;
    bool waypointOffscreen = false;
    glm::vec2 waypointScreenPos{0.0f};
    std::string waypointLabel;

    void refreshEntries();
    void clampSelection();
    void updateMenu(int screenWidth, int screenHeight);
    void updateWaypoint(int screenWidth, int screenHeight);
    void activateTracking(const MenuEntry& entry);
    void clearTracking();
    bool projectWaypoint(int screenWidth, int screenHeight, const glm::vec3& worldPos,
                         glm::vec2& outScreen, bool& outOffscreen) const;

    static void drawRect(class Shader& shader, const glm::ivec2& pos,
                         const glm::vec2& size, const glm::vec4& color);
    static bool pointInRect(const glm::vec2& point, const glm::ivec2& rectPos,
                            const glm::vec2& rectSize);
};
