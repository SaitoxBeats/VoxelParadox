// Arquivo: VoxelParadox.Client/src/Entities/player/hotbar.hpp
// Papel: declara "hotbar" dentro do subsistema "player" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#pragma once

#pragma region Includes

#include <array>
#include <algorithm>

#include "crafting.hpp"
#include "items/item_catalog.hpp"
#pragma endregion

#pragma region HotbarTypesAndState
class PlayerHotbar {
public:
    static constexpr int HOTBAR_SLOT_COUNT = 9;
    static constexpr int EXTRA_SLOT_COUNT = 21;
    static constexpr int SLOT_COUNT = HOTBAR_SLOT_COUNT;
    static constexpr int TOTAL_STORAGE_SLOTS = HOTBAR_SLOT_COUNT + EXTRA_SLOT_COUNT;
    static constexpr int CRAFT_SLOT_COUNT = 3;
    static constexpr int MAX_STACK_SIZE = 64;

    struct Slot {
        InventoryItem item{};
        int count = 0;

        // Funcao: executa 'empty' na hotbar e no inventario do jogador.
        // Detalhe: centraliza a logica necessaria para encapsular esta etapa especifica do subsistema.
        // Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
        bool empty() const {
            return item.empty() || count <= 0;
        }
    };

    struct PersistentState {
        std::array<Slot, TOTAL_STORAGE_SLOTS> storageSlots{};
        std::array<Slot, CRAFT_SLOT_COUNT> craftSlots{};
        Slot heldSlot{};
        int selectedIndex = 0;
        bool inventoryOpen = false;
    };

    enum class ClickType {
        LEFT = 0,
        RIGHT,
    };

    // Funcao: limpa 'clear' na hotbar e no inventario do jogador.
    // Detalhe: centraliza a logica necessaria para resetar o estado temporario mantido por este componente.
#pragma endregion

#pragma region HotbarPublicApi
    void clear();

    // Funcao: executa 'selectSlot' na hotbar e no inventario do jogador.
    // Detalhe: usa 'index' para encapsular esta etapa especifica do subsistema.
    // Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
    bool selectSlot(int index) {
        if (index < 0 || index >= HOTBAR_SLOT_COUNT) {
            return false;
        }
        selectedIndex = index;
        return true;
    }

    // Funcao: retorna 'getSelectedIndex' na hotbar e no inventario do jogador.
    // Detalhe: centraliza a logica necessaria para expor um dado derivado ou um acesso controlado ao estado interno.
    // Retorno: devolve 'int' com o valor numerico calculado para a proxima decisao do pipeline.
    int getSelectedIndex() const {
        return selectedIndex;
    }

    // Funcao: verifica 'isInventoryOpen' na hotbar e no inventario do jogador.
    // Detalhe: centraliza a logica necessaria para avaliar a condicao consultada com base no estado atual.
    // Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
    bool isInventoryOpen() const {
        return inventoryOpen;
    }

    // Funcao: define 'setInventoryOpen' na hotbar e no inventario do jogador.
    // Detalhe: usa 'open' para aplicar ao componente o valor ou configuracao recebida.
    void setInventoryOpen(bool open) {
        inventoryOpen = open;
    }

    // Funcao: alterna 'toggleInventoryOpen' na hotbar e no inventario do jogador.
    // Detalhe: centraliza a logica necessaria para trocar o estado controlado por esta rotina.
    void toggleInventoryOpen() {
        inventoryOpen = !inventoryOpen;
    }

    // Funcao: tenta 'tryCloseInventory' na hotbar e no inventario do jogador.
    // Detalhe: centraliza a logica necessaria para executar a operacao sem assumir que as pre-condicoes estao garantidas.
    // Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
    bool tryCloseInventory();

    // Funcao: retorna 'getSlot' na hotbar e no inventario do jogador.
    // Detalhe: usa 'index' para expor um dado derivado ou um acesso controlado ao estado interno.
    // Retorno: devolve 'const Slot&' para dar acesso direto ao objeto resolvido por esta rotina.
    const Slot& getSlot(int index) const {
        return getHotbarSlot(index);
    }

    // Funcao: retorna 'getHotbarSlot' na hotbar e no inventario do jogador.
    // Detalhe: usa 'index' para expor um dado derivado ou um acesso controlado ao estado interno.
    // Retorno: devolve 'const Slot&' para dar acesso direto ao objeto resolvido por esta rotina.
    const Slot& getHotbarSlot(int index) const {
        static const Slot emptySlot{};
        if (index < 0 || index >= HOTBAR_SLOT_COUNT) {
            return emptySlot;
        }
        return storageSlots[static_cast<size_t>(index)];
    }

    // Funcao: retorna 'getExtraSlot' na hotbar e no inventario do jogador.
    // Detalhe: usa 'index' para expor um dado derivado ou um acesso controlado ao estado interno.
    // Retorno: devolve 'const Slot&' para dar acesso direto ao objeto resolvido por esta rotina.
    const Slot& getExtraSlot(int index) const {
        static const Slot emptySlot{};
        if (index < 0 || index >= EXTRA_SLOT_COUNT) {
            return emptySlot;
        }
        return storageSlots[static_cast<size_t>(HOTBAR_SLOT_COUNT + index)];
    }

    // Funcao: retorna 'getStorageSlot' na hotbar e no inventario do jogador.
    // Detalhe: usa 'index' para expor um dado derivado ou um acesso controlado ao estado interno.
    // Retorno: devolve 'const Slot&' para dar acesso direto ao objeto resolvido por esta rotina.
    const Slot& getStorageSlot(int index) const {
        static const Slot emptySlot{};
        if (index < 0 || index >= TOTAL_STORAGE_SLOTS) {
            return emptySlot;
        }
        return storageSlots[static_cast<size_t>(index)];
    }

    // Funcao: retorna 'getSelectedSlot' na hotbar e no inventario do jogador.
    // Detalhe: centraliza a logica necessaria para expor um dado derivado ou um acesso controlado ao estado interno.
    // Retorno: devolve 'const Slot&' para dar acesso direto ao objeto resolvido por esta rotina.
    const Slot& getSelectedSlot() const {
        return storageSlots[static_cast<size_t>(selectedIndex)];
    }

    // Funcao: retorna 'getSelectedType' na hotbar e no inventario do jogador.
    // Detalhe: centraliza a logica necessaria para expor um dado derivado ou um acesso controlado ao estado interno.
    // Retorno: devolve 'BlockId' com o resultado composto por esta chamada.
    const InventoryItem& getSelectedItem() const {
        return getSelectedSlot().item;
    }

    // Funcao: retorna 'getSelectedCount' na hotbar e no inventario do jogador.
    // Detalhe: centraliza a logica necessaria para expor um dado derivado ou um acesso controlado ao estado interno.
    // Retorno: devolve 'int' com o valor numerico calculado para a proxima decisao do pipeline.
    int getSelectedCount() const {
        return getSelectedSlot().count;
    }

    // Funcao: verifica 'hasSelectedItem' na hotbar e no inventario do jogador.
    // Detalhe: centraliza a logica necessaria para avaliar a condicao consultada com base no estado atual.
    // Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
    bool hasSelectedItem() const {
        return !getSelectedSlot().empty();
    }

    // Funcao: retorna 'getHeldSlot' na hotbar e no inventario do jogador.
    // Detalhe: centraliza a logica necessaria para expor um dado derivado ou um acesso controlado ao estado interno.
    // Retorno: devolve 'const Slot&' para dar acesso direto ao objeto resolvido por esta rotina.
    const Slot& getHeldSlot() const {
        return heldSlot;
    }

    // Funcao: retorna 'getCraftSlot' na hotbar e no inventario do jogador.
    // Detalhe: usa 'index' para expor um dado derivado ou um acesso controlado ao estado interno.
    // Retorno: devolve 'const Slot&' para dar acesso direto ao objeto resolvido por esta rotina.
    const Slot& getCraftSlot(int index) const {
        static const Slot emptySlot{};
        if (index < 0 || index >= CRAFT_SLOT_COUNT) {
            return emptySlot;
        }
        return craftSlots[static_cast<size_t>(index)];
    }

    // Funcao: retorna 'getCraftingResult' na hotbar e no inventario do jogador.
    // Detalhe: centraliza a logica necessaria para expor um dado derivado ou um acesso controlado ao estado interno.
    // Retorno: devolve 'CraftingRecipeResult' com o resultado composto por esta chamada.
    CraftingRecipeResult getCraftingResult() const;

    // Funcao: retorna 'getCraftingBulkResult' na hotbar e no inventario do jogador.
    // Detalhe: centraliza a logica necessaria para expor um dado derivado ou um acesso controlado ao estado interno.
    // Retorno: devolve 'CraftingRecipeResult' com o resultado composto por esta chamada.
    CraftingRecipeResult getCraftingBulkResult() const;

    // Funcao: verifica 'canAccept' na hotbar e no inventario do jogador.
    // Detalhe: usa 'type', 'amount' para avaliar a condicao consultada com base no estado atual.
    // Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
    bool canAccept(const InventoryItem& item, int amount = 1) const;

    // Funcao: cria 'addItem' na hotbar e no inventario do jogador.
    // Detalhe: usa 'type', 'amount' para registrar ou anexar o recurso solicitado no fluxo atual.
    // Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
    bool addItem(const InventoryItem& item, int amount = 1);

    // Funcao: consome 'consumeSelected' na hotbar e no inventario do jogador.
    // Detalhe: usa 'amount' para retirar um pedido ou evento pendente para evitar reprocessamento.
    // Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
    bool consumeSelected(int amount = 1);

    // Funcao: processa 'clickStorageSlot' na hotbar e no inventario do jogador.
    // Detalhe: usa 'index', 'clickType' para interpretar a interacao recebida e executar as mudancas necessarias.
    // Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
    bool clickStorageSlot(int index, ClickType clickType);

    // Funcao: processa 'clickCraftSlot' na hotbar e no inventario do jogador.
    // Detalhe: usa 'index', 'clickType' para interpretar a interacao recebida e executar as mudancas necessarias.
    // Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
    bool clickCraftSlot(int index, ClickType clickType);

    // Funcao: processa 'clickCraftResult' na hotbar e no inventario do jogador.
    // Detalhe: usa 'clickType', 'craftAll' para interpretar a interacao recebida e executar as mudancas necessarias.
    // Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
    bool clickCraftResult(ClickType clickType, bool craftAll = false);
    PersistentState capturePersistentState() const;
    void applyPersistentState(const PersistentState& state);
#pragma endregion

#pragma region HotbarPrivateApi
private:
    std::array<Slot, TOTAL_STORAGE_SLOTS> storageSlots{};
    std::array<Slot, CRAFT_SLOT_COUNT> craftSlots{};
    Slot heldSlot{};
    int selectedIndex = 0;
    bool inventoryOpen = false;

    // Funcao: verifica 'canStore' na hotbar e no inventario do jogador.
    // Detalhe: usa 'type' para avaliar a condicao consultada com base no estado atual.
    // Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
    static bool canStore(const InventoryItem& item) {
        return !item.empty();
    }

    // Funcao: normaliza 'sanitize' na hotbar e no inventario do jogador.
    // Detalhe: usa 'slot' para limpar a entrada para reduzir inconsistencias antes do uso.
    static void sanitize(Slot& slot) {
        if (slot.count <= 0 || slot.item.empty()) {
            slot = Slot{};
        }
    }

    // Funcao: verifica 'canSwapIntoLimitedSlot' na hotbar e no inventario do jogador.
    // Detalhe: usa 'held', 'capacity' para avaliar a condicao consultada com base no estado atual.
    // Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
    static bool canSwapIntoLimitedSlot(const Slot& held, int capacity) {
        if (held.empty()) {
            return true;
        }
        return held.count <= capacity;
    }

    // Funcao: tenta 'tryStoreHeldSlotIntoStorage' na hotbar e no inventario do jogador.
    // Detalhe: centraliza a logica necessaria para executar a operacao sem assumir que as pre-condicoes estao garantidas.
    // Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
    bool tryStoreHeldSlotIntoStorage();

    // Funcao: retorna 'getSlotInteractionCapacity' na hotbar e no inventario do jogador.
    // Detalhe: usa 'slot', 'held' para expor um dado derivado ou um acesso controlado ao estado interno.
    // Retorno: devolve 'int' com o valor numerico calculado para a proxima decisao do pipeline.
    static int getSlotInteractionCapacity(const Slot& slot, const Slot* held = nullptr) {
        if (!slot.empty()) {
            return getInventoryItemStackLimit(slot.item);
        }
        if (held && !held->empty()) {
            return getInventoryItemStackLimit(held->item);
        }
        return MAX_STACK_SIZE;
    }

    // Funcao: retorna 'getResolvedCraftingRecipe' na hotbar e no inventario do jogador.
    // Detalhe: centraliza a logica necessaria para expor um dado derivado ou um acesso controlado ao estado interno.
    // Retorno: devolve 'Crafting::TriangleRecipeMatch' com o resultado composto por esta chamada.
    Crafting::TriangleRecipeMatch getResolvedCraftingRecipe() const;

    // Funcao: executa 'availableCapacityFor' na hotbar e no inventario do jogador.
    // Detalhe: usa 'type' para encapsular esta etapa especifica do subsistema.
    // Retorno: devolve 'int' com o valor numerico calculado para a proxima decisao do pipeline.
    int availableCapacityFor(const InventoryItem& item) const;

    // Funcao: processa 'handleLeftClick' na hotbar e no inventario do jogador.
    // Detalhe: usa 'slot', 'capacity' para interpretar a interacao recebida e executar as mudancas necessarias.
    // Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
    bool handleLeftClick(Slot& slot, int capacity);

    // Funcao: processa 'handleRightClick' na hotbar e no inventario do jogador.
    // Detalhe: usa 'slot', 'capacity' para interpretar a interacao recebida e executar as mudancas necessarias.
    // Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
    bool handleRightClick(Slot& slot, int capacity);

    // Funcao: consome 'consumeCraftIngredients' na hotbar e no inventario do jogador.
    // Detalhe: usa 'amountPerSlot' para retirar um pedido ou evento pendente para evitar reprocessamento.
    void consumeCraftIngredients(int amountPerSlot);
};
#pragma endregion
