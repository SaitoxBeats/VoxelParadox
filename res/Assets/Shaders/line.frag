#version 330 core
// Arquivo: res/Assets/Shaders/line.frag
// Papel: shader de fragmento de cor sólida usado por overlays lineares e wireframes.

uniform vec4 uColor;

out vec4 FragColor;

void main() {
    FragColor = uColor;
}
