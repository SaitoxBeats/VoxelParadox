#version 330 core
// Arquivo: res/Assets/Shaders/hud.frag
// Papel: shader de fragmento genérico do HUD.
// Fluxo: suporta tanto quads texturizados comuns quanto glifos monocromáticos de fonte com tint.

in vec2 TexCoords;

out vec4 FragColor;

uniform sampler2D image;
uniform vec4 color;
uniform bool isText;

void main()
{
    if(isText) {
        float sampled = texture(image, TexCoords).r;
        if(sampled < 0.1) discard;
        FragColor = vec4(color.rgb, sampled * color.a);
    } else {
        vec4 texColor = texture(image, TexCoords);
        if(texColor.a < 0.1) discard;
        FragColor = texColor * color;
    }
}
