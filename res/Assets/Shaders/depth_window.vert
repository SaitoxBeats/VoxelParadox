#version 330 core
// Arquivo: res/Assets/Shaders/depth_window.vert
// Papel: shader de vértice da janela de profundidade usada no preview de portal.
// Fluxo: desenha um quad simples que prepara o depth buffer antes da renderização do mundo filho.

layout(location = 0) in vec3 aPos;

void main() {
    gl_Position = vec4(aPos.xy, 0.0, 1.0);
}
