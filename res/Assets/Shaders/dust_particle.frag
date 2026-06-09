#version 330 core
// Arquivo: res/Assets/Shaders/dust_particle.frag
// Papel: shader de fragmento das partículas de poeira.
// Fluxo: aplica a cor global e a alpha individual de cada partícula mantendo um ponto simples e nítido.

in float vAlpha;

uniform vec4 uColor;

out vec4 FragColor;

void main() {
    FragColor = vec4(uColor.rgb, uColor.a * vAlpha);
}
