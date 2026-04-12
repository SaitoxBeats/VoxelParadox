// Arquivo: VoxelParadox.Client/src/Renderer/render/config/inventory_menu_preview_config.hpp
// Papel: declara "inventory menu preview config" dentro do subsistema "render" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#pragma once

#include <glm/glm.hpp>

#include "render/config/inventory_menu_layout.hpp"
#include "render/config/item_preview_config.hpp"


struct InventoryMenuPreviewConfig {
    InventoryMenuLayout layout{};
    HUDItemPreviewConfig item{};
    glm::ivec2 heldSlotSize{44, 44};
    glm::ivec2 heldSlotOffset{10, 10};
    glm::ivec2 tooltipPreviewSize{40, 40};
    glm::ivec2 tooltipPadding{10, 8};
    glm::ivec2 tooltipCursorOffset{14, 14};
    int tooltipGap = 6;
    int tooltipTextGap = 2;
    int tooltipBorderThickness = 2;

    // Funcao: executa 'InventoryMenuPreviewConfig' neste modulo do projeto VoxelParadox.Client.
    // Detalhe: centraliza a logica necessaria para encapsular esta etapa especifica do subsistema.
    InventoryMenuPreviewConfig() {
        item.camera.position = glm::vec3(0.0f, 0.10f, 3.2f);
        item.camera.target = glm::vec3(0.0f, -0.03f, 0.0f);
        item.camera.fovDegrees = 18.0f;
        item.style.clearBackground = false;
        item.style.backgroundColor = glm::vec4(0.0f);
    }
};

namespace HUDInventoryMenuPreview {
inline InventoryMenuPreviewConfig config{};
}
