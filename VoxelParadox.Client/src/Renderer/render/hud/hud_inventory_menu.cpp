// Arquivo: VoxelParadox.Client/src/Renderer/render/hud/hud_inventory_menu.cpp
// Papel: implementa "hud inventory menu" dentro do subsistema "render hud" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#pragma region Includes
#include "hud_inventory_menu.hpp"

#include <cmath>
#include <string>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/engine.hpp"
#include "engine/input.hpp"
#include "items/item_catalog.hpp"
#include "player/player.hpp"
#include "world/world_stack.hpp"
#include "render/inventory_menu_layout.hpp"
#include "render/inventory_menu_preview_config.hpp"
#include "render/renderer.hpp"
#include "hud.hpp"
#pragma endregion

#pragma region InventoryMenuLocalHelpers
namespace {

// Funcao: executa 'expandRect' na exibicao visual do inventario.
// Detalhe: usa 'rect', 'amount' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'glm::ivec4' com o resultado composto por esta chamada.
glm::ivec4 expandRect(const glm::ivec4& rect, int amount) {
    return glm::ivec4(rect.x - amount, rect.y - amount,
                      rect.z + amount * 2, rect.w + amount * 2);
}

// Funcao: executa 'pointInRect' na exibicao visual do inventario.
// Detalhe: usa 'rect', 'x', 'y' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool pointInRect(const glm::ivec4& rect, float x, float y) {
    return x >= static_cast<float>(rect.x) &&
           y >= static_cast<float>(rect.y) &&
           x <= static_cast<float>(rect.x + rect.z) &&
           y <= static_cast<float>(rect.y + rect.w);
}

// Funcao: procura 'findInventoryHoveredSlot' na exibicao visual do inventario.
// Detalhe: usa 'layout', 'mouseX', 'mouseY' para localizar o primeiro elemento que atende ao criterio esperado.
// Retorno: devolve 'InventoryMenuHoveredSlot' com o resultado composto por esta chamada.
InventoryMenuHoveredSlot findInventoryHoveredSlot(const ResolvedInventoryMenuLayout& layout,
                                                  float mouseX, float mouseY) {
    for (int index = 0; index < PlayerHotbar::CRAFT_SLOT_COUNT; index++) {
        if (pointInRect(layout.craftInputRects[static_cast<size_t>(index)], mouseX, mouseY)) {
            return {InventoryMenuHoveredSlotKind::CRAFT_INPUT, index};
        }
    }

    if (pointInRect(layout.craftResultRect, mouseX, mouseY)) {
        return {InventoryMenuHoveredSlotKind::CRAFT_RESULT, 0};
    }

    for (int index = 0; index < PlayerHotbar::EXTRA_SLOT_COUNT; index++) {
        if (pointInRect(layout.inventorySlotRects[static_cast<size_t>(index)], mouseX, mouseY)) {
            return {InventoryMenuHoveredSlotKind::INVENTORY, index};
        }
    }

    for (int index = 0; index < PlayerHotbar::HOTBAR_SLOT_COUNT; index++) {
        if (pointInRect(layout.hotbarSlotRects[static_cast<size_t>(index)], mouseX, mouseY)) {
            return {InventoryMenuHoveredSlotKind::HOTBAR, index};
        }
    }

    return {};
}

struct InventoryTooltipInfo {
    bool visible = false;
    InventoryItem item{};
    std::string name;
    std::string category;
};

struct InventoryTooltipGeometry {
    glm::ivec4 popupRect{0};
    glm::ivec4 previewRect{0};
    glm::ivec2 namePos{0};
    glm::ivec2 categoryPos{0};
};

InventoryTooltipInfo resolveInventoryTooltip(const Player* player,
                                            const InventoryMenuHoveredSlot& hovered) {
    InventoryTooltipInfo tooltip{};
    if (!player || !hovered.valid()) {
        return tooltip;
    }

    if (hovered.kind == InventoryMenuHoveredSlotKind::CRAFT_RESULT) {
        const CraftingRecipeResult result = player->getCraftingResult();
        if (!result.empty()) {
            tooltip.visible = true;
            tooltip.item = result.item;
            tooltip.name = getInventoryItemDisplayName(result.item);
            tooltip.category = getInventoryItemCategoryDisplayName(result.item);
        }
        return tooltip;
    }

    const PlayerHotbar::Slot* hoveredSlotData = nullptr;
    switch (hovered.kind) {
    case InventoryMenuHoveredSlotKind::HOTBAR:
        hoveredSlotData = &player->getHotbar().getHotbarSlot(hovered.index);
        break;
    case InventoryMenuHoveredSlotKind::INVENTORY:
        hoveredSlotData = &player->getExtraInventorySlot(hovered.index);
        break;
    case InventoryMenuHoveredSlotKind::CRAFT_INPUT:
        hoveredSlotData = &player->getCraftSlot(hovered.index);
        break;
    case InventoryMenuHoveredSlotKind::CRAFT_RESULT:
    case InventoryMenuHoveredSlotKind::NONE:
    default:
        break;
    }

    if (hoveredSlotData && !hoveredSlotData->empty()) {
        tooltip.visible = true;
        tooltip.item = hoveredSlotData->item;
        tooltip.name = getInventoryItemDisplayName(hoveredSlotData->item);
        tooltip.category = getInventoryItemCategoryDisplayName(hoveredSlotData->item);
    }

    return tooltip;
}

InventoryTooltipGeometry buildInventoryTooltipGeometry(
    const InventoryMenuPreviewConfig& preview,
    float mouseX,
    float mouseY,
    int screenWidth,
    int screenHeight,
    const glm::vec2& nameSize,
    const glm::vec2& categorySize,
    bool hasCategory
) {
    const int nameWidth = static_cast<int>(std::round(nameSize.x));
    const int nameHeight = static_cast<int>(std::round(nameSize.y));
    const int categoryWidth = hasCategory ? static_cast<int>(std::round(categorySize.x)) : 0;
    const int categoryHeight = hasCategory ? static_cast<int>(std::round(categorySize.y)) : 0;

    const int contentWidth =
        glm::max(preview.tooltipPreviewSize.x, glm::max(nameWidth, categoryWidth));
    const int textHeight =
        nameHeight + (hasCategory ? preview.tooltipTextGap + categoryHeight : 0);
    const int popupWidth = preview.tooltipPadding.x * 2 + contentWidth;
    const int popupHeight =
        preview.tooltipPadding.y * 2 + preview.tooltipPreviewSize.y +
        preview.tooltipGap + textHeight;

    int popupX = static_cast<int>(std::round(mouseX)) + preview.tooltipCursorOffset.x;
    int popupY = static_cast<int>(std::round(mouseY)) + preview.tooltipCursorOffset.y;
    if (popupX + popupWidth > screenWidth - 4) {
        popupX = static_cast<int>(std::round(mouseX)) - preview.tooltipCursorOffset.x -
                 popupWidth;
    }
    if (popupY + popupHeight > screenHeight - 4) {
        popupY = static_cast<int>(std::round(mouseY)) - preview.tooltipCursorOffset.y -
                 popupHeight;
    }
    popupX = glm::clamp(popupX, 4, glm::max(4, screenWidth - popupWidth - 4));
    popupY = glm::clamp(popupY, 4, glm::max(4, screenHeight - popupHeight - 4));

    InventoryTooltipGeometry geometry{};
    geometry.popupRect = glm::ivec4(popupX, popupY, popupWidth, popupHeight);
    geometry.previewRect = glm::ivec4(popupX + preview.tooltipPadding.x,
                                      popupY + preview.tooltipPadding.y,
                                      preview.tooltipPreviewSize.x,
                                      preview.tooltipPreviewSize.y);
    geometry.namePos = glm::ivec2(popupX + preview.tooltipPadding.x,
                                  popupY + preview.tooltipPadding.y +
                                      preview.tooltipPreviewSize.y + preview.tooltipGap);
    geometry.categoryPos = glm::ivec2(
        popupX + preview.tooltipPadding.x,
        geometry.namePos.y + nameHeight + preview.tooltipTextGap
    );

    return geometry;
}

void drawInventoryTooltipRect(Shader& shader, const glm::ivec4& rect, const glm::vec4& color) {
    if (rect.z <= 0 || rect.w <= 0) {
        return;
    }

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(static_cast<float>(rect.x),
                                            static_cast<float>(rect.y), 0.0f));
    model = glm::scale(model, glm::vec3(static_cast<float>(rect.z),
                                        static_cast<float>(rect.w), 1.0f));

    shader.setMat4("model", model);
    shader.setInt("isText", 0);
    shader.setVec4("color", color);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, HUD::getWhiteTexture());

    HUD::bindQuad();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    HUD::unbindQuad();
}

} // namespace

// Funcao: executa 'hudInventoryMenu' na exibicao visual do inventario.
// Detalhe: usa 'player', 'part' para encapsular esta etapa especifica do subsistema.
hudInventoryMenu::hudInventoryMenu(Player* player, InventoryMenuVisualPart part)
    : player(player),
      part(part),
      sectionText("", 0, 0, glm::vec2(1.0f), 18),
      countText("", 0, 0, glm::vec2(0.9f), 14),
      nameText("", 0, 0, glm::vec2(1.0f), 16) {
    sectionText.setColor(glm::vec3(0.92f, 0.94f, 1.0f));
    countText.setColor(glm::vec3(0.88f, 0.90f, 0.95f));
    nameText.setColor(glm::vec3(0.94f, 0.96f, 1.0f));
}

// Funcao: renderiza 'drawRect' na exibicao visual do inventario.
// Detalhe: usa 'shader', 'rect', 'color' para desenhar a saida visual correspondente usando o estado atual.
void hudInventoryMenu::drawRect(Shader& shader, const glm::ivec4& rect, const glm::vec4& color) {
    if (rect.z <= 0 || rect.w <= 0) {
        return;
    }

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(static_cast<float>(rect.x),
                                            static_cast<float>(rect.y), 0.0f));
    model = glm::scale(model, glm::vec3(static_cast<float>(rect.z),
                                        static_cast<float>(rect.w), 1.0f));

    shader.setMat4("model", model);
    shader.setInt("isText", 0);
    shader.setVec4("color", color);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, HUD::getWhiteTexture());

    HUD::bindQuad();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    HUD::unbindQuad();
}

// Funcao: renderiza 'drawLine' na exibicao visual do inventario.
// Detalhe: usa 'shader', 'start', 'end', 'thickness', 'color' para desenhar a saida visual correspondente usando o estado atual.
void hudInventoryMenu::drawLine(Shader& shader, const glm::vec2& start, const glm::vec2& end,
                                float thickness, const glm::vec4& color) {
    const glm::vec2 delta = end - start;
    const float length = glm::length(delta);
    if (length <= 0.001f || thickness <= 0.0f) {
        return;
    }

    const float angle = std::atan2(delta.y, delta.x);

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(start.x, start.y, 0.0f));
    model = glm::rotate(model, angle, glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::translate(model, glm::vec3(0.0f, -thickness * 0.5f, 0.0f));
    model = glm::scale(model, glm::vec3(length, thickness, 1.0f));

    shader.setMat4("model", model);
    shader.setInt("isText", 0);
    shader.setVec4("color", color);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, HUD::getWhiteTexture());

    HUD::bindQuad();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    HUD::unbindQuad();
}

// Funcao: verifica 'isVisible' na exibicao visual do inventario.
// Detalhe: centraliza a logica necessaria para avaliar a condicao consultada com base no estado atual.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
#pragma endregion

#pragma region InventoryMenuInteraction
bool hudInventoryMenu::isVisible() const {
    return player && player->isInventoryOpen();
}

// Funcao: procura 'findHoveredSlot' na exibicao visual do inventario.
// Detalhe: usa 'screenWidth', 'screenHeight', 'mouseX', 'mouseY' para localizar o primeiro elemento que atende ao criterio esperado.
// Retorno: devolve 'InventoryMenuHoveredSlot' com o resultado composto por esta chamada.
InventoryMenuHoveredSlot hudInventoryMenu::findHoveredSlot(int screenWidth,
                                                           int screenHeight,
                                                           float mouseX,
                                                           float mouseY) const {
    if (!player) {
        return {};
    }

    const auto& config = HUDInventoryMenuPreview::config.layout;
    const ResolvedInventoryMenuLayout layout =
        resolveInventoryMenuLayout(config, screenWidth, screenHeight);
    return findInventoryHoveredSlot(layout, mouseX, mouseY);
}

// Funcao: resolve 'resolveSlot' na exibicao visual do inventario.
// Detalhe: usa 'hovered' para traduzir o estado atual para uma resposta concreta usada pelo restante do sistema.
// Retorno: devolve 'const PlayerHotbar::Slot*' para dar acesso direto ao objeto resolvido por esta rotina.
const PlayerHotbar::Slot* hudInventoryMenu::resolveSlot(const InventoryMenuHoveredSlot& hovered) const {
    if (!player || !hovered.valid()) {
        return nullptr;
    }

    switch (hovered.kind) {
    case InventoryMenuHoveredSlotKind::HOTBAR:
        return &player->getHotbar().getHotbarSlot(hovered.index);
    case InventoryMenuHoveredSlotKind::INVENTORY:
        return &player->getExtraInventorySlot(hovered.index);
    case InventoryMenuHoveredSlotKind::CRAFT_INPUT:
        return &player->getCraftSlot(hovered.index);
    case InventoryMenuHoveredSlotKind::CRAFT_RESULT:
    case InventoryMenuHoveredSlotKind::NONE:
    default:
        return nullptr;
    }
}

// Funcao: processa 'handleClick' na exibicao visual do inventario.
// Detalhe: usa 'clickType' para interpretar a interacao recebida e executar as mudancas necessarias.
void hudInventoryMenu::handleClick(PlayerHotbar::ClickType clickType) {
    if (!player || !hoveredSlot.valid()) {
        return;
    }

    const bool shiftDown =
        Input::keyDown(GLFW_KEY_LEFT_SHIFT) || Input::keyDown(GLFW_KEY_RIGHT_SHIFT);

    switch (hoveredSlot.kind) {
    case InventoryMenuHoveredSlotKind::HOTBAR:
        if (shiftDown) {
            player->quickMoveInventoryStorageSlot(hoveredSlot.index);
        } else {
            player->clickInventoryStorageSlot(hoveredSlot.index, clickType);
        }
        break;
    case InventoryMenuHoveredSlotKind::INVENTORY: {
        const int storageIndex = PlayerHotbar::HOTBAR_SLOT_COUNT + hoveredSlot.index;
        if (shiftDown) {
            player->quickMoveInventoryStorageSlot(storageIndex);
        } else {
            player->clickInventoryStorageSlot(storageIndex, clickType);
        }
        break;
    }
    case InventoryMenuHoveredSlotKind::CRAFT_INPUT:
        player->clickCraftSlot(hoveredSlot.index, clickType);
        break;
    case InventoryMenuHoveredSlotKind::CRAFT_RESULT:
        player->clickCraftResult(
            clickType,
            Input::keyDown(GLFW_KEY_LEFT_SHIFT) || Input::keyDown(GLFW_KEY_RIGHT_SHIFT));
        break;
    case InventoryMenuHoveredSlotKind::NONE:
    default:
        break;
    }
}

// Funcao: atualiza 'update' na exibicao visual do inventario.
// Detalhe: usa 'screenWidth', 'screenHeight' para sincronizar o estado derivado com o frame atual.
#pragma endregion

#pragma region InventoryMenuFrameUpdate
void hudInventoryMenu::update(int screenWidth, int screenHeight) {
    if (!isVisible() || part != InventoryMenuVisualPart::BACKGROUND) {
        return;
    }

    float mouseX = 0.0f;
    float mouseY = 0.0f;
    Input::getMousePosFramebuffer(mouseX, mouseY, screenWidth, screenHeight);
    hoveredSlot = findHoveredSlot(screenWidth, screenHeight, mouseX, mouseY);

    if (Input::mousePressed(GLFW_MOUSE_BUTTON_LEFT)) {
        handleClick(PlayerHotbar::ClickType::LEFT);
    }
    if (Input::mousePressed(GLFW_MOUSE_BUTTON_RIGHT)) {
        handleClick(PlayerHotbar::ClickType::RIGHT);
    }
}

// Funcao: renderiza 'drawBackground' na exibicao visual do inventario.
// Detalhe: usa 'shader', 'screenWidth', 'screenHeight' para desenhar a saida visual correspondente usando o estado atual.
#pragma endregion

#pragma region InventoryMenuRendering
void hudInventoryMenu::drawBackground(Shader& shader, int screenWidth, int screenHeight) {
    if (!player) {
        return;
    }

    const auto& config = HUDInventoryMenuPreview::config.layout;
    const ResolvedInventoryMenuLayout layout =
        resolveInventoryMenuLayout(config, screenWidth, screenHeight);

    drawRect(shader, glm::ivec4(0, 0, screenWidth, screenHeight),
             glm::vec4(0.0f, 0.0f, 0.0f, 0.36f));
    drawRect(shader, expandRect(layout.panelRect, 2), glm::vec4(0.10f, 0.12f, 0.18f, 0.85f));
    drawRect(shader, layout.panelRect, glm::vec4(0.0f, 0.0f, 0.0f, 0.65f));

    sectionText.setText("Crafting");
    sectionText.setPosition(layout.craftingLabelPos.x, layout.craftingLabelPos.y);
    sectionText.draw(shader, screenWidth, screenHeight);

    sectionText.setText("Inventory");
    sectionText.setPosition(layout.inventoryLabelPos.x, layout.inventoryLabelPos.y);
    sectionText.draw(shader, screenWidth, screenHeight);

    sectionText.setText("Hotbar");
    sectionText.setPosition(layout.hotbarLabelPos.x, layout.hotbarLabelPos.y);
    sectionText.draw(shader, screenWidth, screenHeight);

    const auto rectCenter = [](const glm::ivec4& rect) {
        return glm::vec2(rect.x + rect.z * 0.5f, rect.y + rect.w * 0.5f);
    };

    const glm::vec2 craftTopCenter = rectCenter(layout.craftInputRects[0]);
    const glm::vec2 craftLeftCenter = rectCenter(layout.craftInputRects[1]);
    const glm::vec2 craftRightCenter = rectCenter(layout.craftInputRects[2]);

    drawLine(shader, craftTopCenter, craftLeftCenter,
             static_cast<float>(config.craftTriangleLineThickness),
             glm::vec4(0.92f, 0.94f, 1.0f, 0.88f));
    drawLine(shader, craftTopCenter, craftRightCenter,
             static_cast<float>(config.craftTriangleLineThickness),
             glm::vec4(0.92f, 0.94f, 1.0f, 0.88f));
    drawLine(shader, craftLeftCenter, craftRightCenter,
             static_cast<float>(config.craftTriangleLineThickness),
             glm::vec4(0.92f, 0.94f, 1.0f, 0.88f));

    auto drawSlotBase = [&](const glm::ivec4& rect, bool hovered,
                            bool selected, bool outputReady) {
        glm::vec4 borderColor(0.20f, 0.24f, 0.32f, 0.82f);
        glm::vec4 fillColor(0.05f, 0.05f, 0.08f, 0.42f);
        int borderSize = config.slotBorderThickness;

        if (outputReady) {
            borderColor = glm::vec4(0.42f, 0.85f, 0.72f, 0.90f);
            fillColor = glm::vec4(0.10f, 0.18f, 0.16f, 0.42f);
        }
        if (hovered) {
            borderColor = glm::vec4(0.75f, 0.82f, 0.94f, 0.95f);
            borderSize = config.hoverBorderThickness;
        }
        if (selected) {
            borderColor = glm::vec4(0.95f, 0.97f, 1.0f, 0.96f);
            borderSize = config.selectedBorderThickness;
            fillColor = glm::vec4(0.18f, 0.20f, 0.30f, 0.30f);
        }

        drawRect(shader, expandRect(rect, borderSize), borderColor);
        drawRect(shader, rect, fillColor);
    };

    const CraftingRecipeResult craftResult = player->getCraftingResult();
    for (int index = 0; index < PlayerHotbar::CRAFT_SLOT_COUNT; index++) {
        const bool hovered =
            hoveredSlot.kind == InventoryMenuHoveredSlotKind::CRAFT_INPUT &&
            hoveredSlot.index == index;
        drawSlotBase(layout.craftInputRects[static_cast<size_t>(index)], hovered, false, false);
    }

    drawSlotBase(layout.craftResultRect,
                 hoveredSlot.kind == InventoryMenuHoveredSlotKind::CRAFT_RESULT,
                 false, !craftResult.empty());

    for (int index = 0; index < PlayerHotbar::EXTRA_SLOT_COUNT; index++) {
        const bool hovered =
            hoveredSlot.kind == InventoryMenuHoveredSlotKind::INVENTORY &&
            hoveredSlot.index == index;
        drawSlotBase(layout.inventorySlotRects[static_cast<size_t>(index)], hovered, false, false);
    }

    for (int index = 0; index < PlayerHotbar::HOTBAR_SLOT_COUNT; index++) {
        const bool hovered =
            hoveredSlot.kind == InventoryMenuHoveredSlotKind::HOTBAR &&
            hoveredSlot.index == index;
        const bool selected = index == player->getSelectedHotbarIndex();
        drawSlotBase(layout.hotbarSlotRects[static_cast<size_t>(index)], hovered, selected, false);
    }

    float mouseX = 0.0f;
    float mouseY = 0.0f;
    Input::getMousePosFramebuffer(mouseX, mouseY, screenWidth, screenHeight);

    (void)mouseX;
    (void)mouseY;
}

// Funcao: renderiza 'drawCounts' na exibicao visual do inventario.
// Detalhe: usa 'shader', 'screenWidth', 'screenHeight' para desenhar a saida visual correspondente usando o estado atual.
void hudInventoryMenu::drawCounts(Shader& shader, int screenWidth, int screenHeight) {
    if (!player) {
        return;
    }

    const auto& config = HUDInventoryMenuPreview::config.layout;
    const auto& preview = HUDInventoryMenuPreview::config;
    const ResolvedInventoryMenuLayout layout =
        resolveInventoryMenuLayout(config, screenWidth, screenHeight);
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    Input::getMousePosFramebuffer(mouseX, mouseY, screenWidth, screenHeight);
    const InventoryMenuHoveredSlot currentHovered =
        findHoveredSlot(screenWidth, screenHeight, mouseX, mouseY);

    auto drawSlotCount = [&](const PlayerHotbar::Slot& slot, const glm::ivec4& rect,
                             bool selected) {
        if (slot.empty() || slot.count <= 1) {
            return;
        }

        countText.setText(std::to_string(slot.count));
        countText.setColor(selected
                               ? glm::vec3(1.0f, 1.0f, 1.0f)
                               : glm::vec3(0.88f, 0.90f, 0.95f));

        const glm::vec2 textSize = countText.measure();
        const int drawX = rect.x + rect.z - static_cast<int>(std::round(textSize.x)) -
                          config.countPadding;
        const int drawY = rect.y + rect.w - static_cast<int>(std::round(textSize.y)) - 6;
        countText.setPosition(drawX, drawY);
        countText.draw(shader, screenWidth, screenHeight);
    };

    for (int index = 0; index < PlayerHotbar::CRAFT_SLOT_COUNT; index++) {
        drawSlotCount(player->getCraftSlot(index),
                      layout.craftInputRects[static_cast<size_t>(index)], false);
    }

    const CraftingRecipeResult bulkCraftResult = player->getCraftingBulkResult();
    if (!bulkCraftResult.empty() && bulkCraftResult.count > 1) {
        PlayerHotbar::Slot pseudoSlot;
        pseudoSlot.item = bulkCraftResult.item;
        pseudoSlot.count = bulkCraftResult.count;
        drawSlotCount(pseudoSlot, layout.craftResultRect, false);
    }

    for (int index = 0; index < PlayerHotbar::EXTRA_SLOT_COUNT; index++) {
        drawSlotCount(player->getExtraInventorySlot(index),
                      layout.inventorySlotRects[static_cast<size_t>(index)], false);
    }

    for (int index = 0; index < PlayerHotbar::HOTBAR_SLOT_COUNT; index++) {
        drawSlotCount(player->getHotbar().getHotbarSlot(index),
                      layout.hotbarSlotRects[static_cast<size_t>(index)],
                      index == player->getSelectedHotbarIndex());
    }

    const PlayerHotbar::Slot& held = player->getHeldInventorySlot();
    if (!held.empty() && held.count > 1) {
        countText.setText(std::to_string(held.count));
        countText.setColor(glm::vec3(1.0f));
        const glm::vec2 textSize = countText.measure();
        const int drawX = static_cast<int>(std::round(mouseX)) + preview.heldSlotOffset.x +
                          preview.heldSlotSize.x - static_cast<int>(std::round(textSize.x));
        const int drawY = static_cast<int>(std::round(mouseY)) + preview.heldSlotOffset.y +
                          preview.heldSlotSize.y - static_cast<int>(std::round(textSize.y)) - 2;
        countText.setPosition(drawX, drawY);
        countText.draw(shader, screenWidth, screenHeight);
    }
}

void hudInventoryMenu::drawTooltip(Shader& shader, int screenWidth, int screenHeight) {
    if (!player) {
        return;
    }

    const auto& preview = HUDInventoryMenuPreview::config;
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    Input::getMousePosFramebuffer(mouseX, mouseY, screenWidth, screenHeight);
    const InventoryMenuHoveredSlot currentHovered =
        findHoveredSlot(screenWidth, screenHeight, mouseX, mouseY);
    const InventoryTooltipInfo tooltip = resolveInventoryTooltip(player, currentHovered);
    if (!tooltip.visible) {
        return;
    }

    nameText.setText(tooltip.name);
    const glm::vec2 nameSize = nameText.measure();
    const glm::vec2 categorySize = tooltip.category.empty()
                                       ? glm::vec2(0.0f)
                                       : nameText.measureText(tooltip.category);
    const InventoryTooltipGeometry geometry = buildInventoryTooltipGeometry(
        preview, mouseX, mouseY, screenWidth, screenHeight, nameSize, categorySize,
        !tooltip.category.empty());

    drawInventoryTooltipRect(shader, expandRect(geometry.popupRect, preview.tooltipBorderThickness),
                             glm::vec4(0.08f, 0.08f, 0.11f, 0.95f));
    drawInventoryTooltipRect(shader, geometry.popupRect, glm::vec4(0.01f, 0.01f, 0.03f, 0.94f));

    nameText.setColor(glm::vec3(0.94f, 0.96f, 1.0f));
    nameText.setPosition(geometry.namePos.x, geometry.namePos.y);
    nameText.draw(shader, screenWidth, screenHeight);

    if (!tooltip.category.empty()) {
        nameText.setColor(glm::vec3(0.76f, 0.80f, 0.88f));
        nameText.setText(tooltip.category);
        nameText.setPosition(geometry.categoryPos.x, geometry.categoryPos.y);
        nameText.draw(shader, screenWidth, screenHeight);
    }
}

// Funcao: renderiza 'draw' na exibicao visual do inventario.
// Detalhe: usa 'shader', 'screenWidth', 'screenHeight' para desenhar a saida visual correspondente usando o estado atual.
void hudInventoryMenu::draw(Shader& shader, int screenWidth, int screenHeight) {
    if (!isVisible()) {
        return;
    }

    switch (part) {
    case InventoryMenuVisualPart::BACKGROUND:
        drawBackground(shader, screenWidth, screenHeight);
        break;
    case InventoryMenuVisualPart::COUNTS:
        drawCounts(shader, screenWidth, screenHeight);
        break;
    case InventoryMenuVisualPart::TOOLTIP:
        drawTooltip(shader, screenWidth, screenHeight);
        break;
    }
}

// Funcao: executa 'hudInventoryMenuPreview' na exibicao visual do inventario.
// Detalhe: usa 'renderer', 'player', 'worldStack' para encapsular esta etapa especifica do subsistema.
hudInventoryMenuPreview::hudInventoryMenuPreview(Renderer* renderer, const Player* player,
                                                 const WorldStack* worldStack,
                                                 InventoryMenuPreviewPart part)
    : renderer(renderer),
      player(player),
      worldStack(worldStack),
      part(part),
      tooltipText("", 0, 0, glm::vec2(1.0f), 16) {}

// Funcao: renderiza 'draw' na exibicao visual do inventario.
// Detalhe: usa 'shader', 'screenWidth', 'screenHeight' para desenhar a saida visual correspondente usando o estado atual.
#pragma endregion

#pragma region InventoryMenuPreview
void hudInventoryMenuPreview::draw(Shader& shader, int screenWidth, int screenHeight) {
    if (!renderer || !player || !player->isInventoryOpen()) {
        return;
    }

    const auto& config = HUDInventoryMenuPreview::config;
    const ResolvedInventoryMenuLayout layout =
        resolveInventoryMenuLayout(config.layout, screenWidth, screenHeight);
    const int depth = worldStack ? worldStack->currentDepth() : 0;
    const FractalWorld* currentWorld = worldStack ? worldStack->currentWorld() : nullptr;
    const float time = static_cast<float>(ENGINE::GETTIME());
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    Input::getMousePosFramebuffer(mouseX, mouseY, screenWidth, screenHeight);

    if (part == InventoryMenuPreviewPart::SLOTS) {
        for (int index = 0; index < PlayerHotbar::EXTRA_SLOT_COUNT; index++) {
            const PlayerHotbar::Slot& slot = player->getExtraInventorySlot(index);
            if (slot.empty()) {
                continue;
            }

            renderer->renderItemPreviewInRect(
                layout.inventorySlotRects[static_cast<size_t>(index)],
                screenHeight,
                config.layout.slotPreviewInset,
                slot.item,
                depth,
                time,
                config.item,
                false,
                1.0f,
                currentWorld);
        }

        for (int index = 0; index < PlayerHotbar::HOTBAR_SLOT_COUNT; index++) {
            const PlayerHotbar::Slot& slot = player->getHotbar().getHotbarSlot(index);
            if (slot.empty()) {
                continue;
            }

            const bool selected = index == player->getSelectedHotbarIndex();
            renderer->renderItemPreviewInRect(
                layout.hotbarSlotRects[static_cast<size_t>(index)],
                screenHeight,
                config.layout.slotPreviewInset,
                slot.item,
                depth,
                time,
                config.item,
                selected,
                selected ? config.item.selectedScaleMultiplier : 1.0f,
                currentWorld);
        }

        for (int index = 0; index < PlayerHotbar::CRAFT_SLOT_COUNT; index++) {
            const PlayerHotbar::Slot& slot = player->getCraftSlot(index);
            if (slot.empty()) {
                continue;
            }

            renderer->renderItemPreviewInRect(
                layout.craftInputRects[static_cast<size_t>(index)],
                screenHeight,
                config.layout.slotPreviewInset,
                slot.item,
                depth,
                time,
                config.item,
                false,
                1.0f,
                currentWorld);
        }

        const CraftingRecipeResult result = player->getCraftingResult();
        if (!result.empty()) {
            renderer->renderItemPreviewInRect(
                layout.craftResultRect,
                screenHeight,
                config.layout.slotPreviewInset,
                result.item,
                depth,
                time,
                config.item,
                true,
                config.item.selectedScaleMultiplier,
                currentWorld);
        }

        const PlayerHotbar::Slot& held = player->getHeldInventorySlot();
        if (!held.empty()) {
            float heldMouseX = 0.0f;
            float heldMouseY = 0.0f;
            Input::getMousePosFramebuffer(heldMouseX, heldMouseY, screenWidth, screenHeight);

            const glm::ivec4 heldRect(
                static_cast<int>(std::round(heldMouseX)) + config.heldSlotOffset.x,
                static_cast<int>(std::round(heldMouseY)) + config.heldSlotOffset.y,
                config.heldSlotSize.x,
                config.heldSlotSize.y
            );
            renderer->renderItemPreviewInRect(
                heldRect,
                screenHeight,
                0,
                held.item,
                depth,
                time,
                config.item,
                true,
                config.item.selectedScaleMultiplier,
                currentWorld);
        }

        return;
    }

    const InventoryMenuHoveredSlot hovered = findInventoryHoveredSlot(layout, mouseX, mouseY);
    const InventoryTooltipInfo tooltip = resolveInventoryTooltip(player, hovered);
    if (!tooltip.visible) {
        return;
    }

    tooltipText.setText(tooltip.name);
    const glm::vec2 nameSize = tooltipText.measure();
    const glm::vec2 categorySize = tooltip.category.empty()
                                       ? glm::vec2(0.0f)
                                       : tooltipText.measureText(tooltip.category);
    const InventoryTooltipGeometry geometry = buildInventoryTooltipGeometry(
        config, mouseX, mouseY, screenWidth, screenHeight, nameSize, categorySize,
        !tooltip.category.empty());

    drawInventoryTooltipRect(shader, geometry.previewRect, glm::vec4(0.01f, 0.01f, 0.03f, 0.94f));

    renderer->renderItemPreviewInRect(
        geometry.previewRect,
        screenHeight,
        0,
        tooltip.item,
        depth,
        time,
        config.item,
        true,
        1.0f,
        currentWorld
    );
}
#pragma endregion
