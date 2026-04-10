// Arquivo: VoxelParadox.Client/src/Renderer/render/hotbar_layout.hpp
// Papel: declara "hotbar layout" dentro do subsistema "render" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#pragma once

#include <array>
#include <glm/glm.hpp>

#include "player/hotbar.hpp"

struct HotbarHUDLayout {
    glm::ivec2 slotSize{40, 40};
    int slotSpacing = 6;
    glm::ivec2 padding{5, 5};
    glm::ivec2 offset{0, 0};
    int barBorderThickness = 2;
    int lifeBarWidthSlots = 9;
    int lifeBarHeight = 10;
    int lifeBarGap = 8;
    int lifeBarBorderThickness = 2;
    int lifeBarFillInset = 0;
    int portalCooldownHorizontalOffset = 42;
    int portalCooldownVerticalOffset = 4;
    int slotPreviewInset = 4;
    int slotBorderThickness = 2;
    int selectedBorderThickness = 3;
    int countPadding = 3;
    int countBottomPadding = 6;
};

struct ResolvedHotbarLayout {
    glm::ivec4 barRect{0};
    std::array<glm::ivec4, PlayerHotbar::SLOT_COUNT> slotRects{};
};

// Funcao: resolve 'resolveHotbarLayout' neste modulo do projeto VoxelParadox.Client.
// Detalhe: usa 'layout', 'screenWidth', 'screenHeight' para traduzir o estado atual para uma resposta concreta usada pelo restante do sistema.
// Retorno: devolve 'ResolvedHotbarLayout' com o resultado composto por esta chamada.
inline int resolveHotbarBarWidth(const HotbarHUDLayout& layout) {
    return layout.padding.x * 2 +
        layout.slotSize.x * PlayerHotbar::SLOT_COUNT +
        layout.slotSpacing * (PlayerHotbar::SLOT_COUNT - 1);
}

inline int resolveHotbarBarHeight(const HotbarHUDLayout& layout) {
    return layout.padding.y * 2 + layout.slotSize.y;
}

inline int resolveHotbarLifeBarWidth(const HotbarHUDLayout& layout) {
    return resolveHotbarBarWidth(layout);
}

inline glm::ivec2 resolveHotbarLifeBarSize(const HotbarHUDLayout& layout) {
    return glm::ivec2(
        resolveHotbarLifeBarWidth(layout),
        layout.lifeBarHeight
    );
}

inline ResolvedHotbarLayout resolveHotbarLayout(
    const HotbarHUDLayout& layout,
    int screenWidth,
    int screenHeight
) {
    ResolvedHotbarLayout resolved{};

    const int barWidth = resolveHotbarBarWidth(layout);
    const int barHeight = resolveHotbarBarHeight(layout);

    const int barX = (screenWidth - barWidth) / 2 + layout.offset.x;
    const int barY = screenHeight - barHeight - layout.offset.y;
    resolved.barRect = glm::ivec4(barX, barY, barWidth, barHeight);

    for (int slotIndex = 0; slotIndex < PlayerHotbar::SLOT_COUNT; slotIndex++) {
        const int slotX =
            barX + layout.padding.x +
            slotIndex * (layout.slotSize.x + layout.slotSpacing);
        const int slotY = barY + layout.padding.y;
        resolved.slotRects[static_cast<size_t>(slotIndex)] =
            glm::ivec4(slotX, slotY, layout.slotSize.x, layout.slotSize.y);
    }

    return resolved;
}
