#version 330 core
// Arquivo: res/Assets/Shaders/line.vert
// Papel: shader de vértice simples para linhas de debug e wireframes.

layout(location = 0) in vec3 aPos;

uniform mat4 uMVP;

void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
}
