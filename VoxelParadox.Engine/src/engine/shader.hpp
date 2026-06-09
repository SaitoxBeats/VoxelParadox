// Arquivo: Engine/src/engine/shader.h
// Papel: declara o wrapper simples de shader usado pelo runtime.
// Fluxo: carrega fonte GLSL, compila programas e cacheia uniforms usados pelo renderer.
// DependÃªncias principais: OpenGL, GLM e `AppPaths`.
#pragma once

// 1. Standard Library
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

// 2. Third-party Libraries
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

// 3. Local Project Modules
#include "path/app_paths.hpp"

class Shader {
public:
    GLuint program = 0;

    bool compileFullscreenShadertoy(
        const char* glslSrc,
        const std::unordered_map<std::string, std::string>& macroOverrides = {}
    ) {
        if (!glslSrc || glslSrc[0] == '\0') {
            return false;
        }

        const std::string wrappedFragment =
            buildFullscreenShadertoyFragment(glslSrc, macroOverrides);
        return compile(fullscreenQuadVertexSource(), wrappedFragment.c_str());
    }

    bool compileFullscreenShadertoyFromFile(
        const char* glslPath,
        const std::unordered_map<std::string, std::string>& macroOverrides = {}
    ) {
        const std::string fragmentSource = readTextFile(glslPath);
        if (fragmentSource.empty()) {
            return false;
        }

        return compileFullscreenShadertoy(fragmentSource.c_str(), macroOverrides);
    }

    bool compile(const char* vertSrc, const char* fragSrc) {
        release();

        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &vertSrc, nullptr);
        glCompileShader(vs);
        if (!checkCompile(vs, "VERTEX")) return false;

        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &fragSrc, nullptr);
        glCompileShader(fs);
        if (!checkCompile(fs, "FRAGMENT")) { glDeleteShader(vs); return false; }

        program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);
        GLint ok; glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[512]; glGetProgramInfoLog(program, 512, nullptr, log);
            printf("Shader link error: %s\n", log);
            glDeleteProgram(program); program = 0;
            uniformLocations.clear();
        }
        glDeleteShader(vs); glDeleteShader(fs);
        return program != 0;
    }

    bool compileFromFiles(const char* vertPath, const char* fragPath) {
        const std::string vertSource = readTextFile(vertPath);
        if (vertSource.empty()) {
            std::printf("Failed to read vertex shader file: %s\n", vertPath);
            return false;
        }

        const std::string fragSource = readTextFile(fragPath);
        if (fragSource.empty()) {
            std::printf("Failed to read fragment shader file: %s\n", fragPath);
            return false;
        }

        return compile(vertSource.c_str(), fragSource.c_str());
    }

    void use() const { glUseProgram(program); }

    void setMat4(const char* n, const glm::mat4& m) const {
        glUniformMatrix4fv(getUniformLocation(n), 1, GL_FALSE, glm::value_ptr(m));
    }
    void setVec3(const char* n, const glm::vec3& v) const {
        glUniform3fv(getUniformLocation(n), 1, glm::value_ptr(v));
    }
    void setVec2(const char* n, const glm::vec2& v) const {
        glUniform2fv(getUniformLocation(n), 1, glm::value_ptr(v));
    }
    void setVec4(const char* n, const glm::vec4& v) const {
        glUniform4fv(getUniformLocation(n), 1, glm::value_ptr(v));
    }
    void setFloat(const char* n, float f) const {
        glUniform1f(getUniformLocation(n), f);
    }
    void setInt(const char* n, int i) const {
        glUniform1i(getUniformLocation(n), i);
    }

    void release() {
        uniformLocations.clear();
        if (program) {
            glDeleteProgram(program);
            program = 0;
        }
    }

    ~Shader() { release(); }

private:
    mutable std::unordered_map<std::string, GLint> uniformLocations;

    static const char* fullscreenQuadVertexSource() {
        return R"(
#version 330 core
layout(location = 0) in vec3 aPos;
out vec2 vUv;
void main() {
    vUv = aPos.xy * 0.5 + 0.5;
    gl_Position = vec4(aPos, 1.0);
}
)";
    }

    static std::string readTextFile(const char* path) {
        if (!path || path[0] == '\0') {
            return {};
        }

        std::ifstream file(AppPaths::resolve(path), std::ios::in | std::ios::binary);
        if (!file.is_open()) {
            return {};
        }

        std::ostringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    static std::string trimCopy(const std::string& value) {
        const std::size_t first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return {};
        }

        const std::size_t last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    }

    static std::string stripVersionDirective(const std::string& source) {
        std::istringstream stream(source);
        std::ostringstream result;
        std::string line;
        bool removedVersion = false;
        bool firstWritten = false;

        while (std::getline(stream, line)) {
            std::string trimmed = trimCopy(line);
            if (!removedVersion && trimmed.rfind("#version", 0) == 0) {
                removedVersion = true;
                continue;
            }

            if (firstWritten) {
                result << '\n';
            }
            result << line;
            firstWritten = true;
        }

        return result.str();
    }

    static std::string stripMacroOverrides(
        const std::string& source,
        const std::unordered_map<std::string, std::string>& macroOverrides
    ) {
        if (macroOverrides.empty()) {
            return source;
        }

        std::istringstream stream(source);
        std::ostringstream result;
        std::string line;
        bool firstWritten = false;

        while (std::getline(stream, line)) {
            const std::string trimmed = trimCopy(line);
            bool skipLine = false;

            if (trimmed.rfind("#define ", 0) == 0) {
                std::istringstream defineStream(trimmed);
                std::string directive;
                std::string key;
                defineStream >> directive >> key;
                skipLine = macroOverrides.find(key) != macroOverrides.end();
            }

            if (skipLine) {
                continue;
            }

            if (firstWritten) {
                result << '\n';
            }
            result << line;
            firstWritten = true;
        }

        return result.str();
    }

    static std::string buildFullscreenShadertoyFragment(
        const std::string& source,
        const std::unordered_map<std::string, std::string>& macroOverrides
    ) {
        const std::string normalizedSource =
            stripMacroOverrides(stripVersionDirective(source), macroOverrides);

        std::ostringstream wrapped;
        wrapped
            << "#version 330 core\n"
            << "in vec2 vUv;\n"
            << "out vec4 FragColor;\n"
            << "uniform vec2 uResolution;\n"
            << "uniform float uTime;\n"
            << "uniform vec4 uMouse;\n"
            << "uniform float uVignetteExtra;\n"
            << "#define iResolution vec3(uResolution, 1.0)\n"
            << "#define iTime uTime\n"
            << "#define iMouse uMouse\n";

        for (const auto& entry : macroOverrides) {
            wrapped << "#define " << entry.first << ' ' << entry.second << "\n";
        }

        wrapped
            << '\n'
            << normalizedSource
            << "\n\nvoid main() {\n"
            << "    mainImage(FragColor, gl_FragCoord.xy);\n"
            << "}\n";

        return wrapped.str();
    }

    bool checkCompile(GLuint s, const char* type) {
        GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512]; glGetShaderInfoLog(s, 512, nullptr, log);
            printf("%s shader error: %s\n", type, log);
            glDeleteShader(s); return false;
        }
        return true;
    }

    GLint getUniformLocation(const char* n) const {
        if (!n || !*n || program == 0) {
            return -1;
        }

        const std::string name(n);
        auto found = uniformLocations.find(name);
        if (found != uniformLocations.end()) {
            return found->second;
        }

        const GLint location = glGetUniformLocation(program, n);
        uniformLocations.emplace(name, location);
        return location;
    }
};
