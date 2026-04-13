function on_pickup(context)
    --context.play_audio_event("portal.enter")
    context.log("[Versal Item][Lua] You picked up a Versal!")
end

function on_update(context, dt)
    -- Reserved for per-frame item logic.
end

function on_use(context)
    local is_first_portal = not context.has_opened_first_portal

    if not context.has_target() then
        context.log("[Versal Item][Lua] Look at a block first.")
        context.push_notification({ { text = "Look at a block first." } })
        return false
    end

    if context.create_portal_for_target_block() then
        if context.get_player_level() <= 1 and is_first_portal then
            on_openPortal(context)
        end
        context.log("[Versal Item][Lua] Portal created on the targeted block.")
        context.remove_item("versal", 1)
        return true
    end

    context.log("[Versal Item][Lua] Could not create a portal right now.")
    return false
end

function on_openPortal(context)
    context.push_notification({
        { text = "Your first portal has opened! " },
        { text = "To enter a universe, simply press ‘F’ whilst looking at the portal, ", color = { 0.31, 1.0, 0.28 } },
        { text = "but if you want to leave the universe, you’ll need a Versal in your hand.", color = { 0.31, 1.0, 0.28 } },
    })
end
