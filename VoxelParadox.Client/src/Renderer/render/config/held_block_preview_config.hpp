// Arquivo: VoxelParadox.Client/src/Renderer/render/config/held_block_preview_config.hpp
// Papel: declara "held block preview config" dentro do subsistema "render" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#pragma once

#include "render/config/hud_3d_preview.hpp"


struct HeldBlockPreviewConfig {
    HUD3DPreviewRequest preview{};
    float spinSpeed = 1.2f;
    glm::vec3 rotationAxis{1.0f, -1.0f, 3.0f};
    float cubeScale = 0.5f;
    glm::vec3 cubeCenter{0.0f, -0.08f, 0.0f};

    // Funcao: executa 'HeldBlockPreviewConfig' neste modulo do projeto VoxelParadox.Client.
    // Detalhe: centraliza a logica necessaria para encapsular esta etapa especifica do subsistema.
    HeldBlockPreviewConfig() {
        preview.layout.anchor = HUD3DPreviewAnchor::BOTTOM_CENTER;
        preview.layout.size = glm::ivec2(64, 64);
        preview.layout.offset = glm::ivec2(0, 0);
        preview.camera.position = glm::vec3(0.0f, 0.10f, 3.2f);
        preview.camera.target = glm::vec3(0.0f, -0.03f, 0.0f);
        preview.camera.fovDegrees = 18.0f;
        preview.style.clearBackground = false;
        preview.style.backgroundColor = glm::vec4(0.02f, 0.02f, 0.04f, 0.0f);
    }
};

namespace HUDHeldBlockPreview {
inline HeldBlockPreviewConfig config{};
}
