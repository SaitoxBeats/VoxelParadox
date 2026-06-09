#version 330 core
// Arquivo: res/Assets/Shaders/dust_particle.vert
// Papel: shader de vértice das partículas de poeira ambiente.
// Fluxo: projeta os pontos no espaço da câmera e ajusta o tamanho do sprite conforme a distância.

layout(location = 0) in vec3 aPos;
layout(location = 1) in float aAlpha;
layout(location = 2) in float aSize;

uniform mat4 uVP;
uniform vec3 uCameraPos;

out float vAlpha;

void main() {
    gl_Position = uVP * vec4(aPos, 1.0);
    float dist = length(aPos - uCameraPos);
    gl_PointSize = clamp(aSize / (1.0 + dist * 0.05), 1.5, 3.5);
    vAlpha = aAlpha;
}
