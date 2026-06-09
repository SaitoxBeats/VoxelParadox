#version 330 core
// Arquivo: res/Assets/Shaders/depth_window.frag
// Papel: shader de fragmento da janela de profundidade do portal.
// Fluxo: força a profundidade escrita para o plano distante para que o preview aninhado possa desenhar limpo.

void main() {
    gl_FragDepth = 1.0;
}
