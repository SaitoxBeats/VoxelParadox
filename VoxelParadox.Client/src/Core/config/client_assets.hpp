#pragma once

// Central place for asset/data paths used by the client runtime.

namespace ClientAssets {

inline constexpr char kFontsDirectory[] = "Assets/Fonts";
inline constexpr char kBlockModelsDirectory[] = "Assets/Models/block";
inline constexpr char kEntityModelsDirectory[] = "Assets/Models/entity";
inline constexpr char kRecipesDirectory[] = "Assets/Recipes";
inline constexpr char kVoxDirectory[] = "Assets/Voxs";
inline constexpr char kControlsConfig[] = "Assets/Config/controls.lua";
inline constexpr char kMotivationalMessagesFile[] = "Assets/texts/motivational.txt";
inline constexpr char kMotivationalMessagesTemplateFile[] = "Assets/texts/motivational.default.txt";
inline constexpr char kUnicodeFallbackFont[] = "Assets/Fonts/ChillBitmap.ttf";
inline constexpr char kSaveWorldDirectory[] = "Saves/worlds";

inline constexpr char kAxeTexture[] = "Assets/Textures/Items/axe.png";
inline constexpr char kPickaxeTexture[] = "Assets/Textures/Items/pickaxe.png";
inline constexpr char kCrosshairTexture[] = "Assets/Textures/Hud/crosshair.png";
inline constexpr char kMembraneWireModelAsset[] = "Assets/Models/block/membrane_wire.obj";
inline constexpr char kGuyModelAsset[] = "Assets/Models/entity/guy.fbx";

inline constexpr char kHudVertexShader[] = "Assets/Shaders/hud.vert";
inline constexpr char kHudFragmentShader[] = "Assets/Shaders/hud.frag";
inline constexpr char kBlockVertexShader[] = "Assets/Shaders/block.vert";
inline constexpr char kBlockFragmentShader[] = "Assets/Shaders/block.frag";
inline constexpr char kDeathScreenShaderToy[] = "Assets/Shaders/death_screen.glsl";

inline constexpr char kStartupStructureEnvVar[] = "VP_ENABLE_STARTUP_STRUCTURE";
inline constexpr char kStartupStructureAsset[] = "Assets/Voxs/castle.vox";

} // namespace ClientAssets
