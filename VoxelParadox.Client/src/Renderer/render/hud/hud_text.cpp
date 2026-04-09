// Arquivo: VoxelParadox.Client/src/Renderer/render/hud/hud_text.cpp
// Papel: implementa "hud text" dentro do subsistema "render hud" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#pragma region Includes
#include "hud_text.hpp"
#include "hud.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <utility>
#include <vector>

#include "Core/config/client_assets.hpp"
#include "path/app_paths.hpp"
#pragma endregion

namespace {

constexpr std::uint32_t kReplacementCodepoint = 0xFFFD;
constexpr std::uint32_t kQuestionMarkCodepoint = static_cast<std::uint32_t>('?');

} // namespace

#pragma region HudTextLifecycle
// Funcao: executa 'hudText' no elemento de texto do HUD.
// Detalhe: usa 'text', 'x', 'y', 'scale', 'fontSize', 'fontPath' para encapsular esta etapa especifica do subsistema.
hudText::hudText(const std::string& text, int x, int y, glm::vec2 scale,
                 int fontSize, const std::string& fontPath)
    : text(text),
      posX(x),
      posY(y),
      scaleAmt(scale),
      fontSize(fontSize),
      fontPath(fontPath),
      color(1.0f, 1.0f, 1.0f) {
    if (this->fontPath.empty()) {
        this->fontPath = HUD::getDefaultFont();
    }
    loadFont();
    setupBuffers();
}

// Funcao: executa 'hudText' no elemento de texto do HUD.
// Detalhe: usa 'text', 'layout', 'scale', 'fontSize', 'fontPath' para encapsular esta etapa especifica do subsistema.
hudText::hudText(const std::string& text, const HUDLayout& layout, glm::vec2 scale,
                 int fontSize, const std::string& fontPath)
    : text(text),
      posX(0),
      posY(0),
      useLayout(true),
      layout(layout),
      scaleAmt(scale),
      fontSize(fontSize),
      fontPath(fontPath),
      color(1.0f, 1.0f, 1.0f) {
    if (this->fontPath.empty()) {
        this->fontPath = HUD::getDefaultFont();
    }
    loadFont();
    setupBuffers();
}

hudText::~hudText() {
    if (vao != 0) {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }
    if (vbo != 0) {
        glDeleteBuffers(1, &vbo);
        vbo = 0;
    }
}

// Funcao: libera 'cleanupSharedFontCache' no elemento de texto do HUD.
// Detalhe: centraliza a logica necessaria para encerrar a etapa e descartar recursos associados.
#pragma endregion

#pragma region HudTextFontCache
std::shared_ptr<hudText::SharedFontData> hudText::getOrLoadFontData(
    const std::string& fontPath, int fontSize) {
    const std::string cacheKey = makeFontCacheKey(fontPath, fontSize);
    auto& cache = sharedFontCache();
    auto found = cache.find(cacheKey);
    if (found != cache.end()) {
        return found->second;
    }

    const std::filesystem::path resolvedFontPath = AppPaths::resolve(fontPath);
    std::ifstream file(resolvedFontPath, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "hudText: Failed to open font file: "
                  << resolvedFontPath.string() << "\n";
        return {};
    }

    const std::streamsize size = file.tellg();
    if (size <= 0) {
        std::cerr << "hudText: Failed to read font file: "
                  << resolvedFontPath.string() << "\n";
        return {};
    }

    file.seekg(0, std::ios::beg);
    auto fontData = std::make_shared<SharedFontData>();
    fontData->fontBuffer.resize(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(fontData->fontBuffer.data()), size)) {
        std::cerr << "hudText: Failed to read font file: "
                  << resolvedFontPath.string() << "\n";
        return {};
    }

    stbtt_fontinfo font;
    int offset = stbtt_GetFontOffsetForIndex(fontData->fontBuffer.data(), 0);
    if (offset == -1) {
        offset = 0;
    }

    if (!stbtt_InitFont(&font, fontData->fontBuffer.data(), offset)) {
        std::cerr << "hudText: stbtt_InitFont failed for "
                  << resolvedFontPath.string() << "\n";
        return {};
    }

    fontData->fontOffset = offset;
    fontData->scale = stbtt_ScaleForPixelHeight(&font, static_cast<float>(fontSize));

    cache[cacheKey] = fontData;
    return fontData;
}

std::vector<std::uint32_t> hudText::decodeUtf8(const std::string& value) {
    std::vector<std::uint32_t> codepoints;
    codepoints.reserve(value.size());

    for (std::size_t i = 0; i < value.size();) {
        const unsigned char c0 = static_cast<unsigned char>(value[i]);

        if (c0 < 0x80) {
            codepoints.push_back(static_cast<std::uint32_t>(c0));
            ++i;
            continue;
        }

        std::uint32_t codepoint = kReplacementCodepoint;
        std::size_t sequenceLength = 1;

        if ((c0 & 0xE0) == 0xC0 && i + 1 < value.size()) {
            const unsigned char c1 = static_cast<unsigned char>(value[i + 1]);
            if ((c1 & 0xC0) == 0x80) {
                const std::uint32_t candidate =
                    ((static_cast<std::uint32_t>(c0 & 0x1F) << 6) |
                     static_cast<std::uint32_t>(c1 & 0x3F));
                if (candidate >= 0x80) {
                    codepoint = candidate;
                    sequenceLength = 2;
                }
            }
        } else if ((c0 & 0xF0) == 0xE0 && i + 2 < value.size()) {
            const unsigned char c1 = static_cast<unsigned char>(value[i + 1]);
            const unsigned char c2 = static_cast<unsigned char>(value[i + 2]);
            if ((c1 & 0xC0) == 0x80 && (c2 & 0xC0) == 0x80) {
                const std::uint32_t candidate =
                    ((static_cast<std::uint32_t>(c0 & 0x0F) << 12) |
                     (static_cast<std::uint32_t>(c1 & 0x3F) << 6) |
                     static_cast<std::uint32_t>(c2 & 0x3F));
                if (candidate >= 0x800 &&
                    !(candidate >= 0xD800 && candidate <= 0xDFFF)) {
                    codepoint = candidate;
                    sequenceLength = 3;
                }
            }
        } else if ((c0 & 0xF8) == 0xF0 && i + 3 < value.size()) {
            const unsigned char c1 = static_cast<unsigned char>(value[i + 1]);
            const unsigned char c2 = static_cast<unsigned char>(value[i + 2]);
            const unsigned char c3 = static_cast<unsigned char>(value[i + 3]);
            if ((c1 & 0xC0) == 0x80 && (c2 & 0xC0) == 0x80 &&
                (c3 & 0xC0) == 0x80) {
                const std::uint32_t candidate =
                    ((static_cast<std::uint32_t>(c0 & 0x07) << 18) |
                     (static_cast<std::uint32_t>(c1 & 0x3F) << 12) |
                     (static_cast<std::uint32_t>(c2 & 0x3F) << 6) |
                     static_cast<std::uint32_t>(c3 & 0x3F));
                if (candidate >= 0x10000 && candidate <= 0x10FFFF) {
                    codepoint = candidate;
                    sequenceLength = 4;
                }
            }
        }

        codepoints.push_back(codepoint);
        i += sequenceLength;
    }

    return codepoints;
}

bool hudText::loadGlyph(SharedFontData& fontData, std::uint32_t codepoint) {
    if (codepoint == '\n' || codepoint == '\r') {
        return false;
    }
    if (fontData.characters.find(codepoint) != fontData.characters.end()) {
        return true;
    }
    if (fontData.missingGlyphs.find(codepoint) != fontData.missingGlyphs.end()) {
        return false;
    }
    if (fontData.fontBuffer.empty()) {
        fontData.missingGlyphs.insert(codepoint);
        return false;
    }
    if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
        fontData.missingGlyphs.insert(codepoint);
        return false;
    }

    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, fontData.fontBuffer.data(), fontData.fontOffset)) {
        fontData.missingGlyphs.insert(codepoint);
        return false;
    }

    const int glyphIndex = stbtt_FindGlyphIndex(&font, static_cast<int>(codepoint));
    if (glyphIndex == 0 && codepoint != static_cast<std::uint32_t>('?')) {
        fontData.missingGlyphs.insert(codepoint);
        return false;
    }

    int width = 0;
    int height = 0;
    int xoff = 0;
    int yoff = 0;
    unsigned char* bitmap = stbtt_GetCodepointBitmap(
        &font, fontData.scale, fontData.scale, static_cast<int>(codepoint), &width,
        &height, &xoff, &yoff);

    int advanceWidth = 0;
    int leftSideBearing = 0;
    stbtt_GetCodepointHMetrics(
        &font, static_cast<int>(codepoint), &advanceWidth, &leftSideBearing);

    unsigned int texture = 0;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    if (bitmap) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RED,
                     GL_UNSIGNED_BYTE, bitmap);
        stbtt_FreeBitmap(bitmap, nullptr);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, 1, 1, 0, GL_RED, GL_UNSIGNED_BYTE,
                     nullptr);
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    const Character character = {
        texture,
        glm::ivec2(width, height),
        glm::ivec2(xoff, yoff),
        static_cast<unsigned int>(advanceWidth * fontData.scale)
    };
    fontData.characters[codepoint] = character;
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void hudText::cleanupSharedFontCache() {
    auto& cache = sharedFontCache();
    for (auto& [key, fontData] : cache) {
        (void)key;
        if (!fontData) continue;
        for (auto& [codepoint, character] : fontData->characters) {
            (void)codepoint;
            GLuint texture = character.TextureID;
            if (texture != 0) {
                glDeleteTextures(1, &texture);
            }
        }
    }
    cache.clear();
}

// Funcao: monta 'makeFontCacheKey' no elemento de texto do HUD.
// Detalhe: usa 'fontPath', 'fontSize' para derivar e compor um valor pronto para a proxima etapa do pipeline.
// Retorno: devolve 'std::string' com o texto pronto para exibicao, lookup ou serializacao.
std::string hudText::makeFontCacheKey(const std::string& fontPath, int fontSize) {
    return fontPath + "#" + std::to_string(fontSize);
}

std::unordered_map<std::string, std::shared_ptr<hudText::SharedFontData>>&
// Funcao: executa 'sharedFontCache' no elemento de texto do HUD.
// Detalhe: centraliza a logica necessaria para encapsular esta etapa especifica do subsistema.
hudText::sharedFontCache() {
    static std::unordered_map<std::string, std::shared_ptr<SharedFontData>> cache;
    return cache;
}

// Funcao: carrega 'loadFont' no elemento de texto do HUD.
// Detalhe: centraliza a logica necessaria para ler dados externos e adapta-los ao formato interno usado pelo jogo.
void hudText::loadFont() {
    if (fontPath.empty()) {
        std::cerr << "hudText: No font path specified!\n";
        return;
    }

    sharedFontData = getOrLoadFontData(fontPath, fontSize);
    fallbackFontData.clear();

    const std::string fallbackFontPath = ClientAssets::kUnicodeFallbackFont;
    if (!fallbackFontPath.empty() && fallbackFontPath != fontPath) {
        if (auto fallback = getOrLoadFontData(fallbackFontPath, fontSize)) {
            fallbackFontData.push_back(std::move(fallback));
        }
    }
}

// Funcao: prepara 'setupBuffers' no elemento de texto do HUD.
// Detalhe: centraliza a logica necessaria para configurar dados auxiliares ou buffers usados nas proximas chamadas.
#pragma endregion

#pragma region HudTextLayoutAndRendering
void hudText::setupBuffers() {
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

const hudText::Character* hudText::resolveCharacter(std::uint32_t codepoint) const {
    auto resolveFromFont = [&](std::shared_ptr<SharedFontData>& fontData)
        -> const Character* {
        if (!fontData) {
            return nullptr;
        }
        if (!loadGlyph(*fontData, codepoint)) {
            return nullptr;
        }
        auto found = fontData->characters.find(codepoint);
        return found != fontData->characters.end() ? &found->second : nullptr;
    };

    if (const Character* character = resolveFromFont(sharedFontData)) {
        return character;
    }

    for (auto& fontData : fallbackFontData) {
        if (const Character* character = resolveFromFont(fontData)) {
            return character;
        }
    }

    if (codepoint != kQuestionMarkCodepoint) {
        if (const Character* fallback = resolveCharacter(kQuestionMarkCodepoint)) {
            return fallback;
        }
    }

    return nullptr;
}

// Funcao: mede 'measure' no elemento de texto do HUD.
// Detalhe: centraliza a logica necessaria para calcular o tamanho final necessario para layout ou ajuste visual.
// Retorno: devolve 'glm::vec2' com o resultado composto por esta chamada.
glm::vec2 hudText::measure() const {
    return measureText(text);
}

// Funcao: mede 'measureText' no elemento de texto do HUD.
// Detalhe: usa 'value' para calcular o tamanho final necessario para layout ou ajuste visual.
// Retorno: devolve 'glm::vec2' com o resultado composto por esta chamada.
glm::vec2 hudText::measureText(const std::string& value) const {
    const float lineHeight = static_cast<float>(fontSize) * scaleAmt.y * 1.4f;
    if (!sharedFontData && fallbackFontData.empty()) {
        return glm::vec2(0.0f, lineHeight);
    }

    float maxWidth = 0.0f;
    float lineWidth = 0.0f;
    int lines = 1;

    for (std::uint32_t codepoint : decodeUtf8(value)) {
        if (codepoint == '\n') {
            maxWidth = std::max(maxWidth, lineWidth);
            lineWidth = 0.0f;
            ++lines;
            continue;
        }

        const Character* ch = resolveCharacter(codepoint);
        if (!ch) {
            continue;
        }
        lineWidth += (static_cast<float>(ch->Advance) * scaleAmt.x);
    }
    maxWidth = std::max(maxWidth, lineWidth);

    const float height = lineHeight * static_cast<float>(lines);
    return glm::vec2(maxWidth, height);
}

// Funcao: renderiza 'draw' no elemento de texto do HUD.
// Detalhe: usa 'shader', 'screenWidth', 'screenHeight' para desenhar a saida visual correspondente usando o estado atual.
void hudText::draw(Shader& shader, int screenWidth, int screenHeight) {
    if (!sharedFontData && fallbackFontData.empty()) {
        return;
    }

    shader.setVec4("color", glm::vec4(color, opacity));
    shader.setInt("isText", 1);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao);

    int drawX = posX;
    int drawY = posY;
    if (useLayout) {
        const glm::vec2 size = measure();
        const glm::vec2 resolved = resolveHUDPosition(layout, screenWidth, screenHeight, size);
        drawX = static_cast<int>(std::round(resolved.x));
        drawY = static_cast<int>(std::round(resolved.y));
    }

    float currentX = (float)drawX;
    float currentY = (float)drawY;
    const float lineHeight = (float)fontSize * scaleAmt.y * 1.4f;

    for (std::uint32_t codepoint : decodeUtf8(text)) {
        if (codepoint == '\n') {
            currentX = (float)drawX;
            currentY += lineHeight;
            continue;
        }

        const Character* ch = resolveCharacter(codepoint);
        if (!ch) {
            continue;
        }

        float xpos = currentX + ch->Bearing.x * scaleAmt.x;
        float ypos = currentY + ch->Bearing.y * scaleAmt.y + (float)fontSize;

        float w = ch->Size.x * scaleAmt.x;
        float h = ch->Size.y * scaleAmt.y;

        float vertices[6][4] = {
            { xpos,     ypos,       0.0f, 0.0f }, // TL
            { xpos,     ypos + h,   0.0f, 1.0f }, // BL
            { xpos + w, ypos + h,   1.0f, 1.0f }, // BR

            { xpos + w, ypos + h,   1.0f, 1.0f }, // BR
            { xpos + w, ypos,       1.0f, 0.0f }, // TR
            { xpos,     ypos,       0.0f, 0.0f }  // TL
        };

        glBindTexture(GL_TEXTURE_2D, ch->TextureID);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        glm::mat4 model = glm::mat4(1.0f);
        shader.setMat4("model", model);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        currentX += (static_cast<float>(ch->Advance) * scaleAmt.x);
    }
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    shader.setInt("isText", 0);
}
#pragma endregion
