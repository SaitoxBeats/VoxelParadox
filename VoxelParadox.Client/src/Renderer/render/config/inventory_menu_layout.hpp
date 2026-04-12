// Arquivo: VoxelParadox.Client/src/Renderer/render/config/inventory_menu_layout.hpp
// Papel: declara "inventory menu layout" dentro do subsistema "render" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#pragma once

#include <array>
#include <glm/glm.hpp>

#include "player/hotbar.hpp"


struct InventoryMenuLayout {
    glm::ivec2 panelPadding{24, 24};
    glm::ivec2 slotSize{40, 40};
    glm::ivec2 craftResultSize{36, 36};
    int slotSpacing = 6;
    int sectionSpacing = 24;
    int sectionTitleHeight = 18;
    int sectionTitleGap = 12;
    int craftTriangleHalfWidth = 60;
    int craftTriangleHeight = 118;
    int craftTriangleLineThickness = 3;
    int slotPreviewInset = 4;
    int slotBorderThickness = 2;
    int selectedBorderThickness = 3;
    int hoverBorderThickness = 2;
    int countPadding = 3;
    int itemNameHeight = 18;
    int itemNameGap = 14;
    int minimumPanelWidth = 430;
};

struct ResolvedInventoryMenuLayout {
    glm::ivec4 panelRect{0};
    glm::ivec2 craftingLabelPos{0};
    glm::ivec2 inventoryLabelPos{0};
    glm::ivec2 hotbarLabelPos{0};
    glm::ivec2 itemNamePos{0};
    std::array<glm::ivec4, PlayerHotbar::CRAFT_SLOT_COUNT> craftInputRects{};
    glm::ivec4 craftResultRect{0};
    std::array<glm::ivec4, PlayerHotbar::EXTRA_SLOT_COUNT> inventorySlotRects{};
    std::array<glm::ivec4, PlayerHotbar::HOTBAR_SLOT_COUNT> hotbarSlotRects{};
};

// Funcao: resolve 'resolveInventoryMenuLayout' neste modulo do projeto VoxelParadox.Client.
// Detalhe: usa 'layout', 'screenWidth', 'screenHeight' para traduzir o estado atual para uma resposta concreta usada pelo restante do sistema.
// Retorno: devolve 'ResolvedInventoryMenuLayout' com o resultado composto por esta chamada.
inline ResolvedInventoryMenuLayout resolveInventoryMenuLayout(const InventoryMenuLayout& layout,
                                                              int screenWidth,
                                                              int screenHeight) {
    ResolvedInventoryMenuLayout resolved{};

    constexpr int inventoryColumns = PlayerHotbar::EXTRA_SLOT_COLUMN_COUNT;
    constexpr int inventoryRows = PlayerHotbar::EXTRA_SLOT_ROW_COUNT;

    const int hotbarWidth =
        layout.slotSize.x * PlayerHotbar::HOTBAR_SLOT_COUNT +
        layout.slotSpacing * (PlayerHotbar::HOTBAR_SLOT_COUNT - 1);
    const int inventoryWidth =
        layout.slotSize.x * inventoryColumns +
        layout.slotSpacing * (inventoryColumns - 1);
    const int contentWidth = glm::max(glm::max(hotbarWidth, inventoryWidth),
                                      layout.minimumPanelWidth);

    const int craftingSectionHeight =
        layout.sectionTitleHeight + layout.sectionTitleGap +
        layout.slotSize.y + layout.craftTriangleHeight;
    const int inventorySectionHeight =
        layout.sectionTitleHeight + layout.sectionTitleGap +
        layout.slotSize.y * inventoryRows + layout.slotSpacing * (inventoryRows - 1);
    const int hotbarSectionHeight =
        layout.sectionTitleHeight + layout.sectionTitleGap + layout.slotSize.y;

    const int panelWidth = layout.panelPadding.x * 2 + contentWidth;
    const int panelHeight =
        layout.panelPadding.y * 2 + craftingSectionHeight +
        layout.sectionSpacing + inventorySectionHeight +
        layout.sectionSpacing + hotbarSectionHeight +
        layout.itemNameGap + layout.itemNameHeight;

    const int panelX = (screenWidth - panelWidth) / 2;
    const int panelY = (screenHeight - panelHeight) / 2;
    resolved.panelRect = glm::ivec4(panelX, panelY, panelWidth, panelHeight);

    const int contentX = panelX + layout.panelPadding.x;
    const int contentCenterX = contentX + contentWidth / 2;
    int cursorY = panelY + layout.panelPadding.y;

    resolved.craftingLabelPos = glm::ivec2(contentX, cursorY);
    cursorY += layout.sectionTitleHeight + layout.sectionTitleGap;

    const int topCenterY = cursorY + layout.slotSize.y / 2;
    const int bottomCenterY = topCenterY + layout.craftTriangleHeight;

    resolved.craftInputRects[0] =
        glm::ivec4(contentCenterX - layout.slotSize.x / 2,
                   topCenterY - layout.slotSize.y / 2,
                   layout.slotSize.x, layout.slotSize.y);
    resolved.craftInputRects[1] =
        glm::ivec4(contentCenterX - layout.craftTriangleHalfWidth - layout.slotSize.x / 2,
                   bottomCenterY - layout.slotSize.y / 2,
                   layout.slotSize.x, layout.slotSize.y);
    resolved.craftInputRects[2] =
        glm::ivec4(contentCenterX + layout.craftTriangleHalfWidth - layout.slotSize.x / 2,
                   bottomCenterY - layout.slotSize.y / 2,
                   layout.slotSize.x, layout.slotSize.y);

    const int resultCenterY = topCenterY + (layout.craftTriangleHeight * 2) / 3;
    resolved.craftResultRect =
        glm::ivec4(contentCenterX - layout.craftResultSize.x / 2,
                   resultCenterY - layout.craftResultSize.y / 2,
                   layout.craftResultSize.x, layout.craftResultSize.y);

    cursorY += layout.slotSize.y + layout.craftTriangleHeight + layout.sectionSpacing;
    resolved.inventoryLabelPos = glm::ivec2(contentX, cursorY);
    cursorY += layout.sectionTitleHeight + layout.sectionTitleGap;

    const int inventoryStartX =
        panelX + (panelWidth - inventoryWidth) / 2;
    for (int row = 0; row < inventoryRows; row++) {
        for (int col = 0; col < inventoryColumns; col++) {
            const int index = row * inventoryColumns + col;
            resolved.inventorySlotRects[static_cast<size_t>(index)] =
                glm::ivec4(inventoryStartX + col * (layout.slotSize.x + layout.slotSpacing),
                           cursorY + row * (layout.slotSize.y + layout.slotSpacing),
                           layout.slotSize.x, layout.slotSize.y);
        }
    }

    cursorY += layout.slotSize.y * inventoryRows +
               layout.slotSpacing * (inventoryRows - 1) + layout.sectionSpacing;
    resolved.hotbarLabelPos = glm::ivec2(contentX, cursorY);
    cursorY += layout.sectionTitleHeight + layout.sectionTitleGap;

    const int hotbarStartX = panelX + (panelWidth - hotbarWidth) / 2;
    for (int index = 0; index < PlayerHotbar::HOTBAR_SLOT_COUNT; index++) {
        resolved.hotbarSlotRects[static_cast<size_t>(index)] =
            glm::ivec4(hotbarStartX + index * (layout.slotSize.x + layout.slotSpacing),
                       cursorY, layout.slotSize.x, layout.slotSize.y);
    }

    resolved.itemNamePos = glm::ivec2(panelX + panelWidth / 2,
                                      cursorY + layout.slotSize.y + layout.itemNameGap);

    return resolved;
}
