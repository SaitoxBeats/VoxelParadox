// File: VoxelParadox.Client/src/Renderer/render/hud/hud_chat_background.hpp
// Purpose: declares the dedicated chat background inside the HUD subsystem.

#pragma once

#include "hud_element.hpp"

class GameChat;

class hudChatBackground : public HUDElement {
public:
    explicit hudChatBackground(const GameChat* chat);

    void draw(class Shader& shader, int screenWidth, int screenHeight) override;

private:
    const GameChat* chat = nullptr;

    void drawRect(class Shader& shader, const glm::ivec4& rect,
                  const glm::vec4& color) const;
    void drawPanel(class Shader& shader, const glm::ivec4& rect,
                   const glm::vec4& borderColor,
                   const glm::vec4& fillColor) const;
    bool shouldDrawTopBackground() const;
};
