function on_use(context)
    if not context.has_target() then
        context.log("[Versal Item][Lua] Look at a block first.")
        return false
    end

    if context.create_portal_for_target_block() then
        context.log("[Versal Item][Lua] Portal created on the targeted block.")
        return true
    end

    context.log("[Versal Item][Lua] Could not create a portal right now.")
    return false
end