// Arquivo: VoxelParadox.Client/src/Renderer/render/hud/hud_hotbar.cpp
// Papel: implementa "hud hotbar" dentro do subsistema "render hud" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#pragma region Includes
#include "hud_hotbar.hpp"

#include <cmath>
#include <string>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/engine.hpp"
#include "player/player.hpp"
#include "world/persistence/world_stack.hpp"
#include "render/config/hotbar_preview_config.hpp"
#include "render/core/renderer.hpp"
#include "hud.hpp"
#pragma endregion

#pragma region HudHotbarImplementation
namespace {
// Funcao: executa 'expandRect' na exibicao visual da hotbar.
// Detalhe: usa 'rect', 'amount' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'glm::ivec4' com o resultado composto por esta chamada.
glm::ivec4 expandRect(const glm::ivec4& rect, int amount) {
    return glm::ivec4(rect.x - amount, rect.y - amount,
                      rect.z + amount * 2, rect.w + amount * 2);
}
}

// Funcao: executa 'hudHotbar' na exibicao visual da hotbar.
// Detalhe: usa 'player', 'part' para encapsular esta etapa especifica do subsistema.
hudHotbar::hudHotbar(const Player* player, HotbarVisualPart part)
    : player(player),
      part(part),
      countText("", 0, 0, glm::vec2(0.9f), 20) {
    countText.setColor(HUDHotbarPreview::config.style.countTextColor);
}

// Funcao: renderiza 'drawRect' na exibicao visual da hotbar.
// Detalhe: usa 'shader', 'rect', 'color' para desenhar a saida visual correspondente usando o estado atual.
void hudHotbar::drawRect(Shader& shader, const glm::ivec4& rect, const glm::vec4& color) {
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

// Funcao: renderiza 'drawBackground' na exibicao visual da hotbar.
// Detalhe: usa 'shader', 'screenWidth', 'screenHeight' para desenhar a saida visual correspondente usando o estado atual.
void hudHotbar::drawBackground(Shader& shader, int screenWidth, int screenHeight) {
    if (!player || player->isInventoryOpen()) {
        return;
    }

    const HotbarPreviewConfig& config = HUDHotbarPreview::config;
    const ResolvedHotbarLayout layout =
        resolveHotbarLayout(config.layout, screenWidth, screenHeight);

    drawRect(shader, expandRect(layout.barRect, config.layout.barBorderThickness),
             config.style.barBorderColor);
    drawRect(shader, layout.barRect, config.style.barFillColor);

    for (int slotIndex = 0; slotIndex < PlayerHotbar::SLOT_COUNT; slotIndex++) {
        const glm::ivec4 slotRect = layout.slotRects[static_cast<size_t>(slotIndex)];
        drawRect(shader, expandRect(slotRect, config.layout.slotBorderThickness),
                 config.style.slotBorderColor);
        drawRect(shader, slotRect, config.style.slotFillColor);
    }
}

// Funcao: renderiza 'drawSelection' na exibicao visual da hotbar.
// Detalhe: usa 'shader', 'screenWidth', 'screenHeight' para desenhar a saida visual correspondente usando o estado atual.
void hudHotbar::drawSelection(Shader& shader, int screenWidth, int screenHeight) {
    if (!player || player->isInventoryOpen()) {
        return;
    }

    const HotbarPreviewConfig& config = HUDHotbarPreview::config;
    const ResolvedHotbarLayout layout =
        resolveHotbarLayout(config.layout, screenWidth, screenHeight);
    const int selectedIndex = player->getSelectedHotbarIndex();
    if (selectedIndex < 0 || selectedIndex >= PlayerHotbar::SLOT_COUNT) {
        return;
    }

    const glm::ivec4 slotRect = layout.slotRects[static_cast<size_t>(selectedIndex)];
    drawRect(shader, expandRect(slotRect, config.layout.selectedBorderThickness),
             config.style.selectionBorderColor);
    drawRect(shader, slotRect, config.style.selectionFillColor);
}

// Funcao: renderiza 'drawCounts' na exibicao visual da hotbar.
// Detalhe: usa 'shader', 'screenWidth', 'screenHeight' para desenhar a saida visual correspondente usando o estado atual.
void hudHotbar::drawCounts(Shader& shader, int screenWidth, int screenHeight) {
    if (!player || player->isInventoryOpen()) {
        return;
    }

    const HotbarPreviewConfig& config = HUDHotbarPreview::config;
    const ResolvedHotbarLayout layout =
        resolveHotbarLayout(config.layout, screenWidth, screenHeight);
    const PlayerHotbar& hotbar = player->getHotbar();

    for (int slotIndex = 0; slotIndex < PlayerHotbar::SLOT_COUNT; slotIndex++) {
        const PlayerHotbar::Slot& slot = hotbar.getSlot(slotIndex);
        if (slot.empty() || slot.count <= 1) {
            continue;
        }

        const glm::ivec4 slotRect = layout.slotRects[static_cast<size_t>(slotIndex)];
        const bool selected = slotIndex == player->getSelectedHotbarIndex();
        const std::string countString = std::to_string(slot.count);
        countText.setText(countString);
        countText.setColor(selected
                               ? config.style.selectedCountTextColor
                               : config.style.countTextColor);

        const glm::vec2 textSize = countText.measureText(countString);
        const int drawX = slotRect.x + slotRect.z -
                          static_cast<int>(std::round(textSize.x)) -
                          config.layout.countPadding;
        const int drawY = slotRect.y + slotRect.w -
                          static_cast<int>(std::round(textSize.y)) -
                          config.layout.countBottomPadding;
        countText.setPosition(drawX, drawY);
        countText.draw(shader, screenWidth, screenHeight);
    }
}

// Funcao: renderiza 'draw' na exibicao visual da hotbar.
// Detalhe: usa 'shader', 'screenWidth', 'screenHeight' para desenhar a saida visual correspondente usando o estado atual.
void hudHotbar::draw(Shader& shader, int screenWidth, int screenHeight) {
    switch (part) {
    case HotbarVisualPart::BACKGROUND:
        drawBackground(shader, screenWidth, screenHeight);
        break;
    case HotbarVisualPart::SELECTION:
        drawSelection(shader, screenWidth, screenHeight);
        break;
    case HotbarVisualPart::COUNTS:
        drawCounts(shader, screenWidth, screenHeight);
        break;
    }
}

// Funcao: executa 'hudHotbarPreview' na exibicao visual da hotbar.
// Detalhe: usa 'renderer', 'player', 'worldStack' para encapsular esta etapa especifica do subsistema.
hudHotbarPreview::hudHotbarPreview(Renderer* renderer, const Player* player,
                                   const WorldStack* worldStack)
    : renderer(renderer), player(player), worldStack(worldStack) {}

// Funcao: renderiza 'draw' na exibicao visual da hotbar.
// Detalhe: usa 'shader', 'screenWidth', 'screenHeight' para desenhar a saida visual correspondente usando o estado atual.
void hudHotbarPreview::draw(Shader& shader, int screenWidth, int screenHeight) {
    (void)shader;
    (void)screenWidth;
    (void)screenHeight;

    if (!renderer || !player || !worldStack) {
        return;
    }
    if (player->isInventoryOpen()) {
        return;
    }

    renderer->renderHUD3DOverlays(*player, worldStack->currentWorld(),
                                  worldStack->currentDepth(),
                                  static_cast<float>(ENGINE::GETTIME()));
}
#pragma endregion
