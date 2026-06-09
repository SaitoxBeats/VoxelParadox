#version 330 core
// Arquivo: res/Assets/Shaders/hud.vert
// Papel: shader de vértice genérico do HUD 2D.
// Fluxo: posiciona quads de interface na tela usando projeção ortográfica e transformações por elemento.

layout (location = 0) in vec4 vertex;

out vec2 TexCoords;

uniform mat4 projection;
uniform mat4 model;

void main()
{
    gl_Position = projection * model * vec4(vertex.xy, 0.0, 1.0);
    TexCoords = vertex.zw;
}
