#pragma once

// Named HUD groups/layers make the runtime UI much easier to navigate than
// repeating raw strings everywhere.

namespace RuntimeHudIds {

inline constexpr char kDebugInfo[] = "hud_Debug_Info";
inline constexpr char kFpsOnly[] = "hud_Fps_Only";
inline constexpr char kPlayerStatus[] = "hud_player_status";
inline constexpr char kHotbar[] = "hud_hotbar";
inline constexpr char kInventoryMenu[] = "hud_inventory_menu";
inline constexpr char kPauseMenu[] = "hud_pause_menu";
inline constexpr char kSettingsMenu[] = "hud_settings_menu";
inline constexpr char kSettingsGeneralTab[] = "hud_settings_general_tab";
inline constexpr char kSettingsVideoTab[] = "hud_settings_video_tab";
inline constexpr char kSettingsSoundTab[] = "hud_settings_sound_tab";
inline constexpr char kSettingsControlsTab[] = "hud_settings_controls_tab";
inline constexpr char kSettingsConfirmMenu[] = "hud_settings_confirm_menu";
inline constexpr char kSettingsControlsCaptureMenu[] =
    "hud_settings_controls_capture_menu";
inline constexpr char kPortalTrackerWaypoint[] = "hud_portal_tracker_waypoint";
inline constexpr char kPortalTrackerMenu[] = "hud_portal_tracker_menu";
inline constexpr char kSaveToast[] = "hud_save_toast";
inline constexpr char kDeathScreen[] = "hud_death_screen";

inline constexpr char kChatHistory[] = "hud_chat_history";
inline constexpr char kChatInput[] = "hud_chat_input";
inline constexpr char kChatSuggestions[] = "hud_chat_suggestions";

inline constexpr int kDebugLayer = 99;
inline constexpr int kPlayerStatusLayer = 10;
inline constexpr int kInventoryMenuLayer = 90;
inline constexpr int kPauseMenuLayer = 100;
inline constexpr int kSettingsMenuLayer = 110;
inline constexpr int kSettingsTabLayer = 111;
inline constexpr int kSettingsConfirmLayer = 120;
inline constexpr int kSettingsControlsCaptureLayer = 121;
inline constexpr int kPortalTrackerWaypointLayer = 14;
inline constexpr int kPortalTrackerMenuLayer = 95;
inline constexpr int kSaveToastLayer = 16;
inline constexpr int kDeathScreenLayer = 130;

} // namespace RuntimeHudIds
