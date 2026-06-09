#version 330 core
// Arquivo: res/Assets/Shaders/item_sprite.frag
// Papel: shader de fragmento para sprites de item.
// Fluxo: amostra a textura do item e preserva a transparência em previews e item segurado.

in vec2 vUV;

uniform sampler2D uTexture;
uniform float uAlpha;

out vec4 FragColor;

void main() {
    vec4 texColor = texture(uTexture, vUV);
    if (texColor.a < 0.1) discard;
    FragColor = vec4(texColor.rgb, texColor.a * uAlpha);
}
