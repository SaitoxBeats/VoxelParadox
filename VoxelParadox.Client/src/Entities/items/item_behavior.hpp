// File: VoxelParadox.Client/src/Entities/items/item_behavior.hpp
// Purpose: declares stateless runtime behavior families for data-driven items.
// Flow: item definitions point to these behaviors through a string id resolved by the registry.

#pragma once

// 1. Standard Library
#include <string_view>

// 2. Local Project Modules
#include "items/item_types.hpp"

struct ItemDefinition;
struct ItemUseContext;

class ItemBehavior {
public:
    virtual ~ItemBehavior() = default;

    virtual bool onUse(const ItemDefinition& definition, ItemUseContext& context) const;
    virtual bool isTool(const ItemDefinition& definition) const;
    virtual ToolDefinition getToolDefinition(const ItemDefinition& definition) const;
    virtual int getStackLimit(const ItemDefinition& definition) const;
};

class DefaultItemBehavior final : public ItemBehavior {
public:
    bool isTool(const ItemDefinition& definition) const override;
    ToolDefinition getToolDefinition(const ItemDefinition& definition) const override;
    int getStackLimit(const ItemDefinition& definition) const override;
};

class ToolItemBehavior final : public ItemBehavior {
public:
    bool isTool(const ItemDefinition& definition) const override;
    ToolDefinition getToolDefinition(const ItemDefinition& definition) const override;
    int getStackLimit(const ItemDefinition& definition) const override;
};

class DeclarativeItemBehavior final : public ItemBehavior {
public:
    bool onUse(const ItemDefinition& definition, ItemUseContext& context) const override;
};

class LuaScriptItemBehavior final : public ItemBehavior {
public:
    bool onUse(const ItemDefinition& definition, ItemUseContext& context) const override;
};

const ItemBehavior& resolveItemBehavior(std::string_view behaviorId);
