// Arquivo: VoxelParadox.Client/src/Renderer/render/hud/hud_inventory_menu.hpp
// Papel: declara "hud inventory menu" dentro do subsistema "render hud" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#pragma once

#pragma region Includes

#include "player/hotbar.hpp"
#include "hud_element.hpp"
#include "hud_text.hpp"
#pragma endregion

#pragma region InventoryMenuTypes
enum class InventoryMenuVisualPart {
    BACKGROUND = 0,
    COUNTS,
    TOOLTIP,
};

enum class InventoryMenuPreviewPart {
    SLOTS = 0,
    TOOLTIP,
};

enum class InventoryMenuHoveredSlotKind {
    NONE = 0,
    HOTBAR,
    INVENTORY,
    CRAFT_INPUT,
    CRAFT_RESULT,
};

struct InventoryMenuHoveredSlot {
    InventoryMenuHoveredSlotKind kind = InventoryMenuHoveredSlotKind::NONE;
    int index = 0;

    // Funcao: executa 'valid' na exibicao visual do inventario.
    // Detalhe: centraliza a logica necessaria para encapsular esta etapa especifica do subsistema.
    // Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
    bool valid() const {
        return kind != InventoryMenuHoveredSlotKind::NONE;
    }
};
#pragma endregion

#pragma region InventoryMenuForwardDeclarations
class Player;
class Renderer;
class WorldStack;
#pragma endregion

#pragma region InventoryMenuApi
class hudInventoryMenu : public HUDElement {
public:
#pragma region InventoryMenuLifecycleAndFrameApi
    // Funcao: executa 'hudInventoryMenu' na exibicao visual do inventario.
    // Detalhe: usa 'player', 'part' para encapsular esta etapa especifica do subsistema.
    hudInventoryMenu(Player* player, WorldStack* worldStack, InventoryMenuVisualPart part);

    // Funcao: atualiza 'update' na exibicao visual do inventario.
    // Detalhe: usa 'screenWidth', 'screenHeight' para sincronizar o estado derivado com o frame atual.
    void update(int screenWidth, int screenHeight) override;
    // Funcao: renderiza 'draw' na exibicao visual do inventario.
    // Detalhe: usa 'shader', 'screenWidth', 'screenHeight' para desenhar a saida visual correspondente usando o estado atual.
    void draw(class Shader& shader, int screenWidth, int screenHeight) override;
#pragma endregion

private:
#pragma region InventoryMenuState
    Player* player = nullptr;
    WorldStack* worldStack = nullptr;
    InventoryMenuVisualPart part = InventoryMenuVisualPart::BACKGROUND;
    hudText sectionText;
    hudText countText;
    hudText nameText;
    InventoryMenuHoveredSlot hoveredSlot{};
#pragma endregion

#pragma region InventoryMenuInteractionHelpers
    // Funcao: renderiza 'drawRect' na exibicao visual do inventario.
    // Detalhe: usa 'shader', 'rect', 'color' para desenhar a saida visual correspondente usando o estado atual.
    void drawRect(class Shader& shader, const glm::ivec4& rect, const glm::vec4& color);
    // Funcao: renderiza 'drawLine' na exibicao visual do inventario.
    // Detalhe: usa 'shader', 'start', 'end', 'thickness', 'color' para desenhar a saida visual correspondente usando o estado atual.
    void drawLine(class Shader& shader, const glm::vec2& start, const glm::vec2& end,
                  float thickness, const glm::vec4& color);
    // Funcao: verifica 'isVisible' na exibicao visual do inventario.
    // Detalhe: centraliza a logica necessaria para avaliar a condicao consultada com base no estado atual.
    // Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
    bool isVisible() const;
    // Funcao: procura 'findHoveredSlot' na exibicao visual do inventario.
    // Detalhe: usa 'screenWidth', 'screenHeight', 'mouseX', 'mouseY' para localizar o primeiro elemento que atende ao criterio esperado.
    // Retorno: devolve 'InventoryMenuHoveredSlot' com o resultado composto por esta chamada.
    InventoryMenuHoveredSlot findHoveredSlot(int screenWidth, int screenHeight, float mouseX,
                                             float mouseY) const;
    // Funcao: resolve 'resolveSlot' na exibicao visual do inventario.
    // Detalhe: usa 'hovered' para traduzir o estado atual para uma resposta concreta usada pelo restante do sistema.
    // Retorno: devolve 'const PlayerHotbar::Slot*' para dar acesso direto ao objeto resolvido por esta rotina.
    const PlayerHotbar::Slot* resolveSlot(const InventoryMenuHoveredSlot& hovered) const;
    // Funcao: processa 'handleClick' na exibicao visual do inventario.
    // Detalhe: usa 'clickType' para interpretar a interacao recebida e executar as mudancas necessarias.
    void handleClick(PlayerHotbar::ClickType clickType);
#pragma endregion

#pragma region InventoryMenuRenderingHelpers
    // Funcao: renderiza 'drawBackground' na exibicao visual do inventario.
    // Detalhe: usa 'shader', 'screenWidth', 'screenHeight' para desenhar a saida visual correspondente usando o estado atual.
    void drawBackground(class Shader& shader, int screenWidth, int screenHeight);
    // Funcao: renderiza 'drawCounts' na exibicao visual do inventario.
    // Detalhe: usa 'shader', 'screenWidth', 'screenHeight' para desenhar a saida visual correspondente usando o estado atual.
    void drawCounts(class Shader& shader, int screenWidth, int screenHeight);
    // Funcao: renderiza 'drawTooltip' na exibicao visual do inventario.
    // Detalhe: usa 'shader', 'screenWidth', 'screenHeight' para desenhar a saida visual correspondente usando o estado atual.
    void drawTooltip(class Shader& shader, int screenWidth, int screenHeight);
#pragma endregion
};
#pragma endregion

#pragma region InventoryMenuPreviewApi
class hudInventoryMenuPreview : public HUDElement {
public:
#pragma region InventoryMenuPreviewPublicApi
    // Funcao: executa 'hudInventoryMenuPreview' na exibicao visual do inventario.
    // Detalhe: usa 'renderer', 'player', 'worldStack' para encapsular esta etapa especifica do subsistema.
    hudInventoryMenuPreview(Renderer* renderer, const Player* player,
                            const WorldStack* worldStack,
                            InventoryMenuPreviewPart part);
    // Funcao: renderiza 'draw' na exibicao visual do inventario.
    // Detalhe: usa 'shader', 'screenWidth', 'screenHeight' para desenhar a saida visual correspondente usando o estado atual.
    void draw(class Shader& shader, int screenWidth, int screenHeight) override;
#pragma endregion

private:
#pragma region InventoryMenuPreviewState
    Renderer* renderer = nullptr;
    const Player* player = nullptr;
    const WorldStack* worldStack = nullptr;
    InventoryMenuPreviewPart part = InventoryMenuPreviewPart::SLOTS;
    hudText tooltipText;
#pragma endregion
};
#pragma endregion
