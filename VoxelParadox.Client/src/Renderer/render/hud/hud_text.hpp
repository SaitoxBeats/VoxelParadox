// Arquivo: VoxelParadox.Client/src/Renderer/render/hud/hud_text.hpp
// Papel: declara "hud text" dentro do subsistema "render hud" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#pragma once

#pragma region Includes

#include "hud_element.hpp"
#include <cstdint>
#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <glm/glm.hpp>
#include <glad/glad.h>
#pragma endregion

#pragma region HudTextApi
class hudText : public HUDElement {
public:
    // Funcao: executa 'hudText' no elemento de texto do HUD.
    // Detalhe: usa 'text', 'x', 'y', 'scale', 'fontSize', 'fontPath' para encapsular esta etapa especifica do subsistema.
    hudText(const std::string& text, int x, int y, glm::vec2 scale, int fontSize, const std::string& fontPath = "");
    // Funcao: executa 'hudText' no elemento de texto do HUD.
    // Detalhe: usa 'text', 'layout', 'scale', 'fontSize', 'fontPath' para encapsular esta etapa especifica do subsistema.
    hudText(const std::string& text, const HUDLayout& layout, glm::vec2 scale,
            int fontSize, const std::string& fontPath = "");
    // Funcao: libera '~hudText' no elemento de texto do HUD.
    // Detalhe: centraliza a logica necessaria para encerrar a etapa e descartar recursos associados.
    ~hudText() override;
    // Funcao: libera 'cleanupSharedFontCache' no elemento de texto do HUD.
    // Detalhe: centraliza a logica necessaria para encerrar a etapa e descartar recursos associados.
    static void cleanupSharedFontCache();

    // Funcao: define 'setText' no elemento de texto do HUD.
    // Detalhe: usa 'newText' para aplicar ao componente o valor ou configuracao recebida.
    void setText(const std::string& newText) { text = newText; }
    // Funcao: retorna 'getText' no elemento de texto do HUD.
    // Detalhe: centraliza a logica necessaria para expor um dado derivado ou um acesso controlado ao estado interno.
    // Retorno: devolve 'const std::string&' com o texto pronto para exibicao, lookup ou serializacao.
    const std::string& getText() const { return text; }
    // Funcao: define 'setColor' no elemento de texto do HUD.
    // Detalhe: usa 'c' para aplicar ao componente o valor ou configuracao recebida.
    void setColor(const glm::vec3& c) { color = c; }
    void setOpacity(float value) { opacity = value; }
    // Funcao: retorna 'getColor' no elemento de texto do HUD.
    // Detalhe: centraliza a logica necessaria para expor um dado derivado ou um acesso controlado ao estado interno.
    // Retorno: devolve 'const glm::vec3&' para dar acesso direto ao objeto resolvido por esta rotina.
    const glm::vec3& getColor() const { return color; }
    float getOpacity() const { return opacity; }
    // Funcao: define 'setPosition' no elemento de texto do HUD.
    // Detalhe: usa 'x', 'y' para aplicar ao componente o valor ou configuracao recebida.
    void setPosition(int x, int y) { posX = x; posY = y; useLayout = false; }
    // Funcao: define 'setLayout' no elemento de texto do HUD.
    // Detalhe: usa 'newLayout' para aplicar ao componente o valor ou configuracao recebida.
    void setLayout(const HUDLayout& newLayout) { layout = newLayout; useLayout = true; }
    // Funcao: retorna 'getX' no elemento de texto do HUD.
    // Detalhe: centraliza a logica necessaria para expor um dado derivado ou um acesso controlado ao estado interno.
    // Retorno: devolve 'int' com o valor numerico calculado para a proxima decisao do pipeline.
    int getX() const { return posX; }
    // Funcao: retorna 'getY' no elemento de texto do HUD.
    // Detalhe: centraliza a logica necessaria para expor um dado derivado ou um acesso controlado ao estado interno.
    // Retorno: devolve 'int' com o valor numerico calculado para a proxima decisao do pipeline.
    int getY() const { return posY; }
    // Funcao: retorna 'getScale' no elemento de texto do HUD.
    // Detalhe: centraliza a logica necessaria para expor um dado derivado ou um acesso controlado ao estado interno.
    // Retorno: devolve 'glm::vec3' com o resultado composto por esta chamada.
    glm::vec2 getScale() const { return scaleAmt; }
    // Funcao: retorna 'getFontSize' no elemento de texto do HUD.
    // Detalhe: centraliza a logica necessaria para expor um dado derivado ou um acesso controlado ao estado interno.
    // Retorno: devolve 'int' com o valor numerico calculado para a proxima decisao do pipeline.
    int getFontSize() const { return fontSize; }
    // Funcao: mede 'measure' no elemento de texto do HUD.
    // Detalhe: centraliza a logica necessaria para calcular o tamanho final necessario para layout ou ajuste visual.
    // Retorno: devolve 'glm::vec2' com o resultado composto por esta chamada.
    glm::vec2 measure() const;
    // Funcao: mede 'measureText' no elemento de texto do HUD.
    // Detalhe: usa 'value' para calcular o tamanho final necessario para layout ou ajuste visual.
    // Retorno: devolve 'glm::vec2' com o resultado composto por esta chamada.
    glm::vec2 measureText(const std::string& value) const;
    // Funcao: renderiza 'draw' no elemento de texto do HUD.
    // Detalhe: usa 'shader', 'screenWidth', 'screenHeight' para desenhar a saida visual correspondente usando o estado atual.
    void draw(class Shader& shader, int screenWidth, int screenHeight) override;

private:
    struct Character {
        unsigned int TextureID; // ID handle of the glyph texture
        glm::ivec2   Size;      // Size of glyph
        glm::ivec2   Bearing;   // Offset from baseline to left/top of glyph
        unsigned int Advance;   // Horizontal offset to advance to next glyph
    };

    struct SharedFontData {
        std::vector<unsigned char> fontBuffer;
        int fontOffset = 0;
        float scale = 1.0f;
        std::unordered_map<std::uint32_t, Character> characters;
        std::unordered_set<std::uint32_t> missingGlyphs;
    };

    std::string text;
    int posX;
    int posY;
    bool useLayout = false;
    HUDLayout layout{};
    glm::vec2 scaleAmt;
    int fontSize;
    std::string fontPath;

    unsigned int vao = 0;
    unsigned int vbo = 0;
    mutable std::shared_ptr<SharedFontData> sharedFontData;
    mutable std::vector<std::shared_ptr<SharedFontData>> fallbackFontData;
    glm::vec3 color;
    float opacity = 1.0f;

    // Funcao: carrega 'loadFont' no elemento de texto do HUD.
    // Detalhe: centraliza a logica necessaria para ler dados externos e adapta-los ao formato interno usado pelo jogo.
    void loadFont();
    // Funcao: prepara 'setupBuffers' no elemento de texto do HUD.
    // Detalhe: centraliza a logica necessaria para configurar dados auxiliares ou buffers usados nas proximas chamadas.
    void setupBuffers();
    static std::shared_ptr<SharedFontData> getOrLoadFontData(const std::string& fontPath,
                                                             int fontSize);
    static std::vector<std::uint32_t> decodeUtf8(const std::string& value);
    static bool loadGlyph(SharedFontData& fontData, std::uint32_t codepoint);
    const Character* resolveCharacter(std::uint32_t codepoint) const;
    // Funcao: monta 'makeFontCacheKey' no elemento de texto do HUD.
    // Detalhe: usa 'fontPath', 'fontSize' para derivar e compor um valor pronto para a proxima etapa do pipeline.
    // Retorno: devolve 'std::string' com o texto pronto para exibicao, lookup ou serializacao.
    static std::string makeFontCacheKey(const std::string& fontPath, int fontSize);
    // Funcao: executa 'sharedFontCache' no elemento de texto do HUD.
    // Detalhe: centraliza a logica necessaria para encapsular esta etapa especifica do subsistema.
    // Retorno: devolve 'std::unordered_map<std::string, std::shared_ptr<SharedFontData>>&' com o texto pronto para exibicao, lookup ou serializacao.
    static std::unordered_map<std::string, std::shared_ptr<SharedFontData>>& sharedFontCache();
};
#pragma endregion
