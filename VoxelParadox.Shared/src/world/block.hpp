// Arquivo: VoxelParadox.Client/src/World/world/block.hpp
// Papel: declara "block" dentro do subsistema "world" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#pragma once

#pragma region Includes
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <glm/glm.hpp>
#pragma endregion

#pragma region BlockApi
enum class BlockType : uint8_t {
  AIR = 0,
  STONE,
  CRYSTAL,
  VOID_MATTER,
  MEMBRANE,
  ORGANIC,
  METAL,
  PORTAL,
  MEMBRANE_WEAVE,
  MEMBRANE_WIRE,
  COUNT
};

constexpr std::uint32_t BLOCK_TAG_NONE = 0u;
constexpr std::uint32_t BLOCK_TAG_MINEABLE_WITH_AXE = 1u << 0;
constexpr std::uint32_t BLOCK_TAG_MINEABLE_WITH_PICKAXE = 1u << 1;

struct BlockProperties {
  bool solid = false;
  bool transparent = false;
  bool interactable = false;
  bool emitsLight = false;
  bool hasEntity = false;
  std::uint32_t tags = BLOCK_TAG_NONE;
  float hardness = 0.0f;
};

inline BlockProperties getBlockProperties(BlockType type) {
  switch (type) {
  case BlockType::AIR:
    return {false, false, false, false, false, BLOCK_TAG_NONE, 0.0f};
  case BlockType::CRYSTAL:
    return {true, false, false, true, false, BLOCK_TAG_MINEABLE_WITH_PICKAXE, 250.0f};
  case BlockType::PORTAL:
    return {true, false, false, true, false, BLOCK_TAG_NONE, 25.0f};
  case BlockType::MEMBRANE:
    return {true, false, false, false, false, BLOCK_TAG_MINEABLE_WITH_AXE, 0.75f};
  case BlockType::ORGANIC:
    return {true, false, false, false, false, BLOCK_TAG_MINEABLE_WITH_AXE, 0.75f};
  case BlockType::MEMBRANE_WEAVE:
    return {true, false, false, false, false, BLOCK_TAG_MINEABLE_WITH_AXE, 1.5f};
  case BlockType::MEMBRANE_WIRE:
    return {false, true, false, false, false, BLOCK_TAG_NONE, 0.01f};
  case BlockType::STONE:
    return {true, false, false, false, false, BLOCK_TAG_MINEABLE_WITH_PICKAXE, 7.5f};
  case BlockType::VOID_MATTER:
    return {true, false, false, false, false, BLOCK_TAG_MINEABLE_WITH_PICKAXE, 7.5f};
  case BlockType::METAL:
    return {true, false, false, false, false, BLOCK_TAG_MINEABLE_WITH_PICKAXE, 25.0f};
  default:
    return {};
  }
}

// Funcao: verifica 'isSolid' na definicao de blocos do jogo.
// Detalhe: usa 't' para avaliar a condicao consultada com base no estado atual.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
inline bool isSolid(BlockType t) {
  return getBlockProperties(t).solid;
}
// Funcao: verifica 'isEmissive' na definicao de blocos do jogo.
// Detalhe: usa 't' para avaliar a condicao consultada com base no estado atual.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
inline bool isEmissive(BlockType t) {
  return getBlockProperties(t).emitsLight;
}

inline bool usesCustomBlockModel(BlockType type) {
  return type == BlockType::MEMBRANE_WIRE;
}

inline bool isReplaceableBlock(BlockType type) {
  return type == BlockType::AIR || usesCustomBlockModel(type);
}

inline bool isPlaceableBlockType(BlockType type) {
  return type != BlockType::AIR &&
         type != BlockType::COUNT &&
         (isSolid(type) || usesCustomBlockModel(type));
}

inline bool canTargetBlock(BlockType type) {
  return type != BlockType::AIR && type != BlockType::COUNT;
}

inline bool getBlockSelectionBounds(BlockType type, glm::vec3& outMin,
                                    glm::vec3& outMax) {
  if (type == BlockType::AIR || type == BlockType::COUNT) {
    return false;
  }

  if (type == BlockType::MEMBRANE_WIRE) {
    outMin = glm::vec3(0.30f, 0.0f, 0.30f);
    outMax = glm::vec3(0.70f, 0.65f, 0.70f);
    return true;
  }

  outMin = glm::vec3(0.0f);
  outMax = glm::vec3(1.0f);
  return true;
}

inline bool canSupportTopPlacedBlock(BlockType supportType, BlockType placedType) {
  if (placedType == BlockType::MEMBRANE_WIRE) {
    return supportType == BlockType::MEMBRANE;
  }

  return isSolid(supportType);
}

inline std::uint32_t getBlockTags(BlockType type) {
  return getBlockProperties(type).tags;
}

inline bool hasBlockTag(BlockType type, std::uint32_t tags) {
  return (getBlockTags(type) & tags) != 0u;
}

inline float getBlockHardness(BlockType type) {
  return getBlockProperties(type).hardness;
}

struct BlockData {
  bool canDropItem = false;
};

// Funcao: retorna 'getBlockData' na definicao de blocos do jogo.
// Detalhe: usa 'type' para expor um dado derivado ou um acesso controlado ao estado interno.
// Retorno: devolve 'BlockData' com o resultado composto por esta chamada.
inline BlockData getBlockData(BlockType type) {
  switch (type) {
  case BlockType::AIR:
    return {false};
  case BlockType::STONE:
    return {false};
  case BlockType::CRYSTAL:
    return {false};
  case BlockType::VOID_MATTER:
    return {true};
  case BlockType::MEMBRANE:
    return {true};
  case BlockType::ORGANIC:
    return {true};
  case BlockType::METAL:
    return {false};
  case BlockType::PORTAL:
    return {false};
  case BlockType::MEMBRANE_WEAVE:
    return {true};
  case BlockType::MEMBRANE_WIRE:
    return {true};
  default:
    return {false};
  }
}

// Funcao: retorna 'getBlockBreakTimeSeconds' na definicao de blocos do jogo.
// Detalhe: usa 'type' para expor um dado derivado ou um acesso controlado ao estado interno.
// Retorno: devolve 'float' com o valor numerico calculado para a proxima decisao do pipeline.
inline float getBlockBreakTimeSeconds(BlockType type) {
  return getBlockHardness(type);
}

// Funcao: verifica 'canBlockDropItem' na definicao de blocos do jogo.
// Detalhe: usa 'type' para avaliar a condicao consultada com base no estado atual.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
inline bool canBlockDropItem(BlockType type) {
  return getBlockData(type).canDropItem;
}

// Funcao: retorna 'getBlockId' na definicao de blocos do jogo.
// Detalhe: usa 'type' para expor um dado derivado ou um acesso controlado ao estado interno.
// Retorno: devolve 'const char*' com o texto pronto para exibicao, lookup ou serializacao.
inline const char* getBlockId(BlockType type) {
  switch (type) {
  case BlockType::AIR:
    return "air";
  case BlockType::STONE:
    return "stone";
  case BlockType::CRYSTAL:
    return "crystal";
  case BlockType::VOID_MATTER:
    return "void_matter";
  case BlockType::MEMBRANE:
    return "membrane";
  case BlockType::ORGANIC:
    return "organic";
  case BlockType::METAL:
    return "metal";
  case BlockType::PORTAL:
    return "portal";
  case BlockType::MEMBRANE_WEAVE:
    return "membrane_weave";
  case BlockType::MEMBRANE_WIRE:
    return "membrane_wire";
  default:
    return "unknown";
  }
}

// Funcao: executa 'normalizeBlockId' na definicao de blocos do jogo.
// Detalhe: usa 'value' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'std::string' com o texto pronto para exibicao, lookup ou serializacao.
inline std::string normalizeBlockId(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) {
                   if (ch == ' ' || ch == '-') {
                     return static_cast<char>('_');
                   }
                   return static_cast<char>(std::tolower(ch));
                 });
  return value;
}

// Funcao: tenta 'tryParseBlockType' na definicao de blocos do jogo.
// Detalhe: usa 'rawValue', 'outType' para executar a operacao sem assumir que as pre-condicoes estao garantidas.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
inline bool tryParseBlockType(const std::string& rawValue, BlockType& outType) {
  const std::string value = normalizeBlockId(rawValue);
  for (int index = 0; index < static_cast<int>(BlockType::COUNT); index++) {
    const BlockType type = static_cast<BlockType>(index);
    if (value == getBlockId(type)) {
      outType = type;
      return true;
    }
  }

  outType = BlockType::AIR;
  return false;
}

// Funcao: retorna 'getBlockDisplayName' na definicao de blocos do jogo.
// Detalhe: usa 'type' para expor um dado derivado ou um acesso controlado ao estado interno.
// Retorno: devolve 'const char*' com o texto pronto para exibicao, lookup ou serializacao.
inline const char* getBlockDisplayName(BlockType type) {
  switch (type) {
  case BlockType::AIR:
    return "Air";
  case BlockType::STONE:
    return "Stone";
  case BlockType::CRYSTAL:
    return "Crystal";
  case BlockType::VOID_MATTER:
    return "Void Matter";
  case BlockType::MEMBRANE:
    return "Membrane";
  case BlockType::ORGANIC:
    return "Organic";
  case BlockType::METAL:
    return "Metal";
  case BlockType::PORTAL:
    return "Portal";
  case BlockType::MEMBRANE_WEAVE:
    return "Membrane Weave";
  case BlockType::MEMBRANE_WIRE:
    return "Membrane Wire";
  default:
    return "Unknown";
  }
}

// HSV helpers
// Funcao: executa 'hsvToRgb' na definicao de blocos do jogo.
// Detalhe: usa 'h', 's', 'v' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'glm::vec3' com o resultado composto por esta chamada.
inline glm::vec3 hsvToRgb(float h, float s, float v) {
  h = std::fmod(h, 1.0f);
  if (h < 0)
    h += 1.0f;
  float c = v * s, x = c * (1.0f - std::fabs(std::fmod(h * 6.0f, 2.0f) - 1.0f)),
        m = v - c;
  glm::vec3 rgb;
  int hi = (int)(h * 6.0f) % 6;
  switch (hi) {
  case 0:
    rgb = {c, x, 0};
    break;
  case 1:
    rgb = {x, c, 0};
    break;
  case 2:
    rgb = {0, c, x};
    break;
  case 3:
    rgb = {0, x, c};
    break;
  case 4:
    rgb = {x, 0, c};
    break;
  default:
    rgb = {c, 0, x};
    break;
  }
  return rgb + glm::vec3(m);
}

// Funcao: executa 'rgbToHsv' na definicao de blocos do jogo.
// Detalhe: usa 'c' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'glm::vec3' com o resultado composto por esta chamada.
inline glm::vec3 rgbToHsv(glm::vec3 c) {
  float mx = glm::max(c.r, glm::max(c.g, c.b));
  float mn = glm::min(c.r, glm::min(c.g, c.b));
  float d = mx - mn, h = 0, s = mx > 0 ? d / mx : 0, v = mx;
  if (d > 0.001f) {
    if (mx == c.r)
      h = std::fmod((c.g - c.b) / d, 6.0f);
    else if (mx == c.g)
      h = (c.b - c.r) / d + 2.0f;
    else
      h = (c.r - c.g) / d + 4.0f;
    h /= 6.0f;
    if (h < 0)
      h += 1.0f;
  }
  return {h, s, v};
}

// Base colors for each block type
// Funcao: retorna 'getBaseColor' na definicao de blocos do jogo.
// Detalhe: usa 'type' para expor um dado derivado ou um acesso controlado ao estado interno.
// Retorno: devolve 'glm::vec3' com o resultado composto por esta chamada.
inline glm::vec3 getBaseColor(BlockType type) {
  switch (type) {
  case BlockType::STONE:
    return {0.45f, 0.42f, 0.50f};
  case BlockType::CRYSTAL:
    return {0.15f, 0.85f, 0.95f};
  case BlockType::VOID_MATTER:
    return {0.20f, 0.08f, 0.30f};
  case BlockType::MEMBRANE:
    return {0.25f, 0.90f, 0.55f};
  case BlockType::ORGANIC:
    return {0.75f, 0.35f, 0.28f};
  case BlockType::METAL:
    return {0.68f, 0.70f, 0.75f};
  case BlockType::PORTAL:
    return {0.95f, 0.20f, 0.85f};
  case BlockType::MEMBRANE_WEAVE:
    return {0.58f, 0.92f, 0.78f};
  case BlockType::MEMBRANE_WIRE:
    return {0.76f, 0.96f, 0.82f};
  default:
    return {1, 0, 1}; // magenta = error
  }
}

// Funcao: retorna 'getBlockMaterialId' na definicao de blocos do jogo.
// Detalhe: usa 'type' para expor um dado derivado ou um acesso controlado ao estado interno.
// Retorno: devolve 'float' com o valor numerico calculado para a proxima decisao do pipeline.
inline float getBlockMaterialId(BlockType type) {
  return static_cast<float>(static_cast<std::uint8_t>(type));
}

// Get the secondary tint applied on top of the material-specific shader look.
// face: 0=-X, 1=+X, 2=-Y, 3=+Y, 4=-Z, 5=+Z
// Funcao: retorna 'getBlockColor' na definicao de blocos do jogo.
// Detalhe: usa 'type', 'depth', 'face' para expor um dado derivado ou um acesso controlado ao estado interno.
// Retorno: devolve 'glm::vec4' com o resultado composto por esta chamada.
inline glm::vec4 getBlockColor(BlockType type, int depth, int face) {
  const glm::vec3 base = glm::max(getBaseColor(type), glm::vec3(0.08f));
  glm::vec3 hsv = rgbToHsv(base);

  // Depth shifts act as a secondary palette modifier, not as the material identity.
  hsv.x = std::fmod(hsv.x + depth * 0.17f, 1.0f);
  hsv.y = glm::min(1.0f, hsv.y + depth * 0.05f);
  hsv.z = glm::clamp(hsv.z + std::sin(depth * 1.3f) * 0.1f, 0.1f, 1.0f);

  const glm::vec3 shifted = hsvToRgb(hsv.x, hsv.y, hsv.z);
  glm::vec3 tint = shifted / base;

  // Keep a tiny per-face variation so the tint still feels a bit alive,
  // while the main lighting remains shader-driven.
  static const float faceTint[] = {0.98f, 1.02f, 0.94f, 1.05f, 0.97f, 1.01f};
  tint *= faceTint[face];
  tint = glm::clamp(glm::mix(glm::vec3(1.0f), tint, 0.45f), glm::vec3(0.55f),
                    glm::vec3(1.55f));

  const float emissive = isEmissive(type)
                             ? glm::clamp(0.48f + std::sin(depth * 1.15f + face * 0.7f) * 0.08f,
                                          0.32f, 0.62f)
                             : 0.0f;
  return glm::vec4(tint, emissive);
}
#pragma endregion
