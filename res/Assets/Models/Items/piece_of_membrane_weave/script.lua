function on_pickup(context)
    -- Reserved for logic when the item is picked up.
end

function on_update(context, dt)
    -- Reserved for per-frame item logic.
end

function on_use(context)
    -- Reserved for logic when the item is used.
    if context.get_player_life() < context.get_player_max_life() then
        context.heal_player(3) -- Heal the player by 10 life points.
        return true -- Indicate that the item was used successfully.
    end
    return false
end

