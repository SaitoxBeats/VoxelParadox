// Arquivo: Engine/src/engine/input.h
// Papel: declara um componente do núcleo compartilhado do engine.
// Fluxo: apoia bootstrap, timing, input, câmera ou render base do projeto.
// Dependências principais: GLFW, GLM e utilitários do engine.
#pragma once

// Arquivo: Engine/src/engine/input.h
// Papel: declara a camada estÃ¡tica de input do projeto.
// Fluxo: o bootstrap registra callbacks GLFW, `update` fecha o snapshot do frame e os
// chamadores consultam teclado, mouse, texto digitado e modo de foco gameplay/UI.
// DependÃªncias principais: GLFW e `std::string`.

#include <GLFW/glfw3.h>
#include <cstring>
#include <string>

class Input {
public:
    enum class FocusMode {
        GAMEPLAY = 0,
        UI,
    };

    static void init(GLFWwindow* w) {
        window = w;
        memset(keys, 0, sizeof(keys));
        memset(prevKeys, 0, sizeof(prevKeys));
        memset(mouseBtn, 0, sizeof(mouseBtn));
        memset(prevMouseBtn, 0, sizeof(prevMouseBtn));
        firstMouse = true;
        mouseDX = mouseDY = 0;
        scrollY = 0;
        typedChars.clear();
        textInputEnabled = false;
        cursorVisible = false;
        focusMode = FocusMode::GAMEPLAY;
        glfwSetKeyCallback(w, keyCallback);
        glfwSetCharCallback(w, charCallback);
        glfwSetCursorPosCallback(w, cursorCallback);
        glfwSetMouseButtonCallback(w, mouseButtonCallback);
        glfwSetScrollCallback(w, scrollCallback);
        glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }

    static void shutdown() {
        if (window) {
            glfwSetKeyCallback(window, nullptr);
            glfwSetCharCallback(window, nullptr);
            glfwSetCursorPosCallback(window, nullptr);
            glfwSetMouseButtonCallback(window, nullptr);
            glfwSetScrollCallback(window, nullptr);
        }

        window = nullptr;
        memset(keys, 0, sizeof(keys));
        memset(prevKeys, 0, sizeof(prevKeys));
        memset(mouseBtn, 0, sizeof(mouseBtn));
        memset(prevMouseBtn, 0, sizeof(prevMouseBtn));
        lastMX = 0.0f;
        lastMY = 0.0f;
        mouseDX = 0.0f;
        mouseDY = 0.0f;
        scrollY = 0.0f;
        mouseX = 0.0f;
        mouseY = 0.0f;
        firstMouse = true;
        textInputEnabled = false;
        cursorVisible = false;
        focusMode = FocusMode::GAMEPLAY;
        typedChars.clear();
    }

    static void update() {
        memcpy(prevKeys, keys, sizeof(keys));
        memcpy(prevMouseBtn, mouseBtn, sizeof(mouseBtn));
        mouseDX = mouseDY = 0;
        scrollY = 0;
        glfwPollEvents();
    }

    static bool keyDown(int k) { return k >= 0 && k < 512 && keys[k]; }
    static bool keyPressed(int k) { return k >= 0 && k < 512 && keys[k] && !prevKeys[k]; }
    static bool mouseDown(int b) { return b >= 0 && b < 8 && mouseBtn[b]; }
    static bool mousePressed(int b) { return b >= 0 && b < 8 && mouseBtn[b] && !prevMouseBtn[b]; }
    static void getMouseDelta(float& dx, float& dy) { dx = mouseDX; dy = mouseDY; }
    static void getMousePos(float& x, float& y) { x = mouseX; y = mouseY; }
    static void getMousePosFramebuffer(float& x, float& y, int framebufferW, int framebufferH) {
        int winW = 1, winH = 1;
        if (window) glfwGetWindowSize(window, &winW, &winH);
        if (winW <= 0) winW = 1;
        if (winH <= 0) winH = 1;
        x = mouseX * ((float)framebufferW / (float)winW);
        y = mouseY * ((float)framebufferH / (float)winH);
    }
    static float getScroll() { return scrollY; }

    static float mouseDX, mouseDY, scrollY;
    static float mouseX, mouseY;

    static void enableTextInput(bool enabled) {
        textInputEnabled = enabled;
        if (!enabled) typedChars.clear();
    }

    static bool isTextInputEnabled() { return textInputEnabled; }

    static std::string consumeTypedChars() {
        std::string out = typedChars;
        typedChars.clear();
        return out;
    }

    static void setClipboardText(const std::string& value) {
        if (!window) return;
        glfwSetClipboardString(window, value.c_str());
    }

    static std::string getClipboardText() {
        if (!window) return {};

        const char* clipboardText = glfwGetClipboardString(window);
        return clipboardText ? std::string(clipboardText) : std::string();
    }

    static void setCursorVisible(bool visible) {
        if (!window) return;
        if (cursorVisible == visible) return;
        cursorVisible = visible;
        glfwSetInputMode(window, GLFW_CURSOR, visible ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
        firstMouse = true;
        mouseDX = mouseDY = 0;
    }

    static bool isCursorVisible() { return cursorVisible; }

    static void setFocusMode(FocusMode mode) {
        if (focusMode == mode) return;
        focusMode = mode;
        firstMouse = true;
        mouseDX = mouseDY = 0;
    }

    static FocusMode getFocusMode() { return focusMode; }
    static void toggleFocusMode() {
        setFocusMode(focusMode == FocusMode::GAMEPLAY ? FocusMode::UI
                                                      : FocusMode::GAMEPLAY);
    }
    static bool hasGameplayFocus() { return focusMode == FocusMode::GAMEPLAY; }
    static bool hasUiFocus() { return focusMode == FocusMode::UI; }

private:
    static GLFWwindow* window;
    static bool keys[512], prevKeys[512];
    static bool mouseBtn[8], prevMouseBtn[8];
    static float lastMX, lastMY;
    static bool firstMouse;
    static bool textInputEnabled;
    static bool cursorVisible;
    static FocusMode focusMode;
    static std::string typedChars;

    static void keyCallback(GLFWwindow*, int key, int, int action, int) {
        if (key >= 0 && key < 512) {
            if (action == GLFW_PRESS) keys[key] = true;
            else if (action == GLFW_RELEASE) keys[key] = false;
        }
    }
    static void charCallback(GLFWwindow*, unsigned int codepoint) {
        if (!textInputEnabled) return;
        if (codepoint < 32 || codepoint > 126) return;
        typedChars.push_back(static_cast<char>(codepoint));
    }
    static void cursorCallback(GLFWwindow*, double x, double y) {
        float fx = (float)x, fy = (float)y;
        mouseX = fx;
        mouseY = fy;
        if (firstMouse) { lastMX = fx; lastMY = fy; firstMouse = false; }
        mouseDX += fx - lastMX;
        mouseDY += fy - lastMY;
        lastMX = fx; lastMY = fy;
    }
    static void mouseButtonCallback(GLFWwindow*, int btn, int action, int) {
        if (btn >= 0 && btn < 8) {
            mouseBtn[btn] = (action == GLFW_PRESS);
        }
    }
    static void scrollCallback(GLFWwindow*, double, double y) { scrollY += (float)y; }
};
