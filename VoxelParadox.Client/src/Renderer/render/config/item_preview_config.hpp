// Arquivo: VoxelParadox.Client/src/Renderer/render/config/item_preview_config.hpp
// Papel: declara "item preview config" dentro do subsistema "render" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#pragma once

#include <glm/glm.hpp>

#include "render/config/hud_3d_preview.hpp"


struct HUDItemPreviewConfig {
    HUD3DPreviewCamera camera{};
    HUD3DPreviewStyle style{};
    glm::vec3 staticRotationDegrees{-20.0f, 35.0f, 10.0f};
    glm::vec3 spinAxis{0.0f, 1.0f, 0.0f};
    float spinSpeed = 1.4f;
    float cubeScale = 0.46f;
    float selectedScaleMultiplier = 1.08f;
    glm::vec3 cubeCenter{0.0f, -0.05f, 0.0f};
};

