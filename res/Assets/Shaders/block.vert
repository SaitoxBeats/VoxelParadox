#version 330 core
// Arquivo: res/Assets/Shaders/block.vert
// Papel: shader de vértice principal dos voxels, usado por chunks, itens cúbicos e superfícies de portal.
// Fluxo: recebe posição, normal, AO, tint e material id e repassa tudo em espaço de mundo
// para o fragment shader gerar o material procedural e os efeitos de quebra.

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aTint;
layout(location = 3) in float aAO;
layout(location = 4) in float aMaterial;

uniform mat4 uVP;
uniform mat4 uModel;

out vec3 vWorldPos;
out vec3 vLocalPos;
out vec3 vNormal;
out vec3 vFaceNormal;
out vec4 vTint;
out float vAO;
flat out int vMaterialId;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vLocalPos = aPos;
    vNormal = mat3(uModel) * aNormal;
    vFaceNormal = aNormal;
    vTint = aTint;
    vAO = aAO;
    vMaterialId = int(aMaterial + 0.5);
    gl_Position = uVP * worldPos;
}
