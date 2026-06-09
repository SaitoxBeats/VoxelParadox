#version 330 core
// Arquivo: res/Assets/Shaders/item_sprite.vert
// Papel: shader de vértice para itens 2D texturizados, como a axe.
// Fluxo: é usado tanto nos previews do HUD quanto no item segurado em primeira pessoa.

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;

uniform mat4 uVP;
uniform mat4 uModel;

out vec2 vUV;

void main() {
    vUV = aUV;
    gl_Position = uVP * uModel * vec4(aPos, 1.0);
}
