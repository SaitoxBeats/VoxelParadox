// Arquivo: Engine/src/engine/engine.cpp
// Papel: implementa um componente do nÃºcleo compartilhado do engine.
// Fluxo: apoia bootstrap, timing, input, cÃ¢mera ou render base do projeto.
// DependÃªncias principais: GLFW, GLM e utilitÃ¡rios do engine.
#include "engine/engine.hpp"
#include "engine/pause_manager.hpp"

// Arquivo: Engine/src/engine/engine.cpp
// Papel: implementa o estado global de frame, mÃƒÂ©tricas de CPU/GPU, pausa e janela.
// Fluxo: o loop principal injeta `now` e `dt`, o renderer fecha queries de GPU e
// qualquer sistema pode consultar viewport, monitor, pausa ou timers suavizados.
// DependÃƒÂªncias principais: GLFW, GLAD, OpenGL e GLM.

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <glm/common.hpp>
#include <utility>
#include <vector>

namespace ENGINE {
namespace {
bool initialized = false;

double nowSecondsCached = 0.0;
float deltaTimeSeconds = 0.0f;

std::uint64_t frameCount = 0;

double fpsSampleIntervalSeconds = 1.0; // 1000ms
float fpsCached = 0.0f;
double fpsAccumulatedSeconds = 0.0;
std::uint64_t fpsAccumulatedFrames = 0;

constexpr float perfSmoothingSeconds = 0.20f;
constexpr float fpsSpikeEnterThreshold = 30.0f;
constexpr float fpsSpikeExitThreshold = 35.0f;
constexpr size_t gpuQueryBufferCount = 4;

struct SmoothedMetric {
  float rawMs = 0.0f;
  float displayMs = 0.0f;
  bool initialized = false;
};

SmoothedMetric cpuFrameMetric{};
SmoothedMetric gpuFrameMetric{};
SmoothedMetric chunkRenderMetric{};
int renderedChunkCountCached = 0;
float frameChunkRenderTotalMs = 0.0f;
int frameChunkRenderCount = 0;

std::array<GLuint, gpuQueryBufferCount> gpuQueries{};
std::array<bool, gpuQueryBufferCount> gpuQueryPending{};
size_t gpuQueryWriteIndex = 0;
bool gpuQueriesInitialized = false;
bool gpuQueryInFlight = false;

bool fpsSpikeActive = false;
int fpsSpikeCount = 0;
float fpsSpikeWorstFps = 0.0f;
float fpsSpikePeakCpuMs = 0.0f;
float fpsSpikePeakGpuMs = 0.0f;

GLFWwindow* attachedWindow = nullptr;
glm::vec2 defaultViewportSize(1280.0f, 720.0f);
glm::vec2 viewportSize = defaultViewportSize;
glm::vec2 monitorSize(0.0f);
glm::vec2 windowPos(0.0f);
glm::vec2 lastWindowedSize = defaultViewportSize;
glm::vec2 lastWindowedPos(100.0f, 100.0f);
VIEWPORTMODE viewportMode = VIEWPORTMODE::WINDOWMODE;
bool vSyncEnabled = false;

int toInt(float value, int fallback = 1) {
  const int rounded = static_cast<int>(value + 0.5f);
  return rounded > 0 ? rounded : fallback;
}

glm::ivec2 resolveFullscreenSize(GLFWmonitor* monitor,
                                 const GLFWvidmode* fallbackMode) {
  if (!monitor || !fallbackMode) {
    return glm::ivec2(toInt(defaultViewportSize.x, 1280),
                      toInt(defaultViewportSize.y, 720));
  }

  const glm::ivec2 requested(toInt(defaultViewportSize.x, fallbackMode->width),
                             toInt(defaultViewportSize.y, fallbackMode->height));

  int videoModeCount = 0;
  const GLFWvidmode* videoModes = glfwGetVideoModes(monitor, &videoModeCount);
  for (int index = 0; index < videoModeCount; index++) {
    if (videoModes[index].width == requested.x &&
        videoModes[index].height == requested.y) {
      return requested;
    }
  }

  return glm::ivec2(fallbackMode->width, fallbackMode->height);
}

float perfBlendAlpha() {
  const float dt = deltaTimeSeconds > 0.0f ? deltaTimeSeconds : (1.0f / 60.0f);
  if (perfSmoothingSeconds <= 0.0f) {
    return 1.0f;
  }
  return std::clamp(dt / perfSmoothingSeconds, 0.0f, 1.0f);
}

void resetMetric(SmoothedMetric& metric) {
  metric.rawMs = 0.0f;
  metric.displayMs = 0.0f;
  metric.initialized = false;
}

void updateMetric(SmoothedMetric& metric, float valueMs) {
  // As metricas exibidas ao jogador sÃƒÂ£o suavizadas para leitura humana, sem perder o valor bruto.
  const float clampedValue = std::max(valueMs, 0.0f);
  metric.rawMs = clampedValue;
  if (!metric.initialized) {
    metric.displayMs = clampedValue;
    metric.initialized = true;
    return;
  }

  const float alpha = perfBlendAlpha();
  metric.displayMs += (clampedValue - metric.displayMs) * alpha;
}

void resetPerfTrackingState() {
  resetMetric(cpuFrameMetric);
  resetMetric(gpuFrameMetric);
  resetMetric(chunkRenderMetric);
  renderedChunkCountCached = 0;
  frameChunkRenderTotalMs = 0.0f;
  frameChunkRenderCount = 0;

  fpsSpikeActive = false;
  fpsSpikeCount = 0;
  fpsSpikeWorstFps = 0.0f;
  fpsSpikePeakCpuMs = 0.0f;
  fpsSpikePeakGpuMs = 0.0f;

  gpuQueryWriteIndex = 0;
  gpuQueryPending.fill(false);
  gpuQueryInFlight = false;
}

void ensureGpuQueries() {
  if (gpuQueriesInitialized) {
    return;
  }

  glGenQueries(static_cast<GLsizei>(gpuQueries.size()), gpuQueries.data());
  gpuQueriesInitialized = true;
  gpuQueryPending.fill(false);
}

void resolveGpuQueries() {
  if (!gpuQueriesInitialized) {
    return;
  }

  for (size_t i = 0; i < gpuQueries.size(); i++) {
    if (!gpuQueryPending[i]) {
      continue;
    }

    GLuint available = 0;
    glGetQueryObjectuiv(gpuQueries[i], GL_QUERY_RESULT_AVAILABLE, &available);
    if (available == 0) {
      continue;
    }

    GLuint64 elapsedNanoseconds = 0;
    glGetQueryObjectui64v(gpuQueries[i], GL_QUERY_RESULT, &elapsedNanoseconds);
    gpuQueryPending[i] = false;
    updateMetric(
        gpuFrameMetric,
        static_cast<float>(static_cast<double>(elapsedNanoseconds) / 1000000.0));
  }
}

void finalizeFpsSpikeIfNeeded(float fpsInstant) {
  if (fpsInstant <= 0.0f) {
    return;
  }

  if (!fpsSpikeActive) {
    if (fpsInstant <= fpsSpikeEnterThreshold) {
      fpsSpikeActive = true;
      fpsSpikeWorstFps = fpsInstant;
      fpsSpikePeakCpuMs = cpuFrameMetric.rawMs;
      fpsSpikePeakGpuMs = gpuFrameMetric.rawMs;
    }
    return;
  }

  fpsSpikeWorstFps = std::min(fpsSpikeWorstFps, fpsInstant);
  fpsSpikePeakCpuMs = std::max(fpsSpikePeakCpuMs, cpuFrameMetric.rawMs);
  fpsSpikePeakGpuMs = std::max(fpsSpikePeakGpuMs, gpuFrameMetric.rawMs);

  if (fpsInstant >= fpsSpikeExitThreshold) {
    fpsSpikeCount++;
    std::printf(
        "[Perf][Fps spike #%d] FPS: %d | GPU: %.2fms Peak | CPU: %.2fms Peak\n",
        fpsSpikeCount, static_cast<int>(std::lround(fpsSpikeWorstFps)),
        fpsSpikePeakGpuMs, fpsSpikePeakCpuMs);
    std::fflush(stdout);

    fpsSpikeActive = false;
    fpsSpikeWorstFps = 0.0f;
    fpsSpikePeakCpuMs = 0.0f;
    fpsSpikePeakGpuMs = 0.0f;
  }
}

void refreshMonitorSize() {
  GLFWmonitor* monitor = glfwGetPrimaryMonitor();
  const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
  if (!mode) return;
  monitorSize = glm::vec2(static_cast<float>(mode->width),
                          static_cast<float>(mode->height));
}

glm::ivec2 resolveBorderlessSize(const GLFWvidmode* videoMode) {
  if (!videoMode) {
    return glm::ivec2(toInt(defaultViewportSize.x, 1280),
                      toInt(defaultViewportSize.y, 720));
  }

  const glm::ivec2 requested(toInt(defaultViewportSize.x, videoMode->width),
                             toInt(defaultViewportSize.y, videoMode->height));
  return glm::clamp(requested, glm::ivec2(64, 64),
                    glm::ivec2(videoMode->width, videoMode->height));
}

glm::ivec2 resolveCenteredWindowPos(GLFWmonitor* monitor,
                                    const GLFWvidmode* videoMode,
                                    const glm::ivec2& windowSize) {
  int monitorX = 0;
  int monitorY = 0;
  if (monitor) {
    glfwGetMonitorPos(monitor, &monitorX, &monitorY);
  }
  if (!videoMode) {
    return glm::ivec2(monitorX, monitorY);
  }

  return glm::ivec2(monitorX + std::max(0, (videoMode->width - windowSize.x) / 2),
                    monitorY + std::max(0, (videoMode->height - windowSize.y) / 2));
}

void syncWindowState() {
  // The cached state must reflect the real window so settings and HUD read the right data.
  refreshMonitorSize();
  if (!attachedWindow) return;

  int fbW = 0, fbH = 0;
  glfwGetFramebufferSize(attachedWindow, &fbW, &fbH);
  if (fbW > 0 && fbH > 0) {
    viewportSize = glm::vec2(static_cast<float>(fbW), static_cast<float>(fbH));
  }

  int posX = 0, posY = 0;
  glfwGetWindowPos(attachedWindow, &posX, &posY);
  windowPos = glm::vec2(static_cast<float>(posX), static_cast<float>(posY));
  if (viewportMode == VIEWPORTMODE::WINDOWMODE) {
    lastWindowedPos = windowPos;

    int winW = 0, winH = 0;
    glfwGetWindowSize(attachedWindow, &winW, &winH);
    if (winW > 0 && winH > 0) {
      lastWindowedSize = glm::vec2(static_cast<float>(winW), static_cast<float>(winH));
    }
  }
}

void framebufferSizeCallback(GLFWwindow*, int width, int height) {
  if (width > 0 && height > 0) {
    viewportSize =
        glm::vec2(static_cast<float>(width), static_cast<float>(height));
  }
}

void windowPosCallback(GLFWwindow*, int x, int y) {
  windowPos = glm::vec2(static_cast<float>(x), static_cast<float>(y));
  if (viewportMode == VIEWPORTMODE::WINDOWMODE) {
    lastWindowedPos = windowPos;
  }
}

void applyWindowMode() {
  // A troca de modo preserva o ÃƒÂºltimo tamanho/janela windowed para um retorno previsÃƒÂ­vel.
  if (!attachedWindow) return;

  refreshMonitorSize();
  GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
  const GLFWvidmode* videoMode =
      primaryMonitor ? glfwGetVideoMode(primaryMonitor) : nullptr;
  int monitorX = 0;
  int monitorY = 0;
  if (primaryMonitor) {
    glfwGetMonitorPos(primaryMonitor, &monitorX, &monitorY);
  }

  if (viewportMode == VIEWPORTMODE::WINDOWMODE) {
    const glm::ivec2 windowedSize =
        glm::ivec2(toInt(lastWindowedSize.x, 1280),
                   toInt(lastWindowedSize.y, 720));
    const glm::ivec2 windowedPos =
        resolveCenteredWindowPos(primaryMonitor, videoMode, windowedSize);

    glfwSetWindowAttrib(attachedWindow, GLFW_DECORATED, GLFW_TRUE);
    glfwSetWindowMonitor(attachedWindow, nullptr, windowedPos.x, windowedPos.y,
                         windowedSize.x, windowedSize.y, GLFW_DONT_CARE);
  } else if (viewportMode == VIEWPORTMODE::BORDERLESS) {
    if (!primaryMonitor || !videoMode) return;

    const glm::ivec2 borderlessSize = resolveBorderlessSize(videoMode);
    const glm::ivec2 borderlessPos =
        resolveCenteredWindowPos(primaryMonitor, videoMode, borderlessSize);
    glfwSetWindowAttrib(attachedWindow, GLFW_DECORATED, GLFW_FALSE);
    glfwSetWindowMonitor(attachedWindow, nullptr, borderlessPos.x, borderlessPos.y,
                         borderlessSize.x, borderlessSize.y, GLFW_DONT_CARE);
  } else {
    if (!primaryMonitor || !videoMode) return;

    glfwSetWindowAttrib(attachedWindow, GLFW_DECORATED, GLFW_TRUE);
    const glm::ivec2 fullscreenSize =
        resolveFullscreenSize(primaryMonitor, videoMode);
    glfwSetWindowMonitor(attachedWindow, primaryMonitor, monitorX, monitorY,
                         fullscreenSize.x, fullscreenSize.y, GLFW_DONT_CARE);
  }

  syncWindowState();
}

void applyVSync() {
  if (!attachedWindow) {
    return;
  }

  GLFWwindow* currentContext = glfwGetCurrentContext();
  if (currentContext != attachedWindow) {
    glfwMakeContextCurrent(attachedWindow);
  }
  glfwSwapInterval(vSyncEnabled ? 1 : 0);
}
} // namespace

bool ISINITIALIZED() {
  return initialized;
}

void INIT(double nowSeconds) {
  initialized = true;
  nowSecondsCached = nowSeconds;
  deltaTimeSeconds = 0.0f;
  frameCount = 0;
  fpsCached = 0.0f;
  fpsAccumulatedSeconds = 0.0;
  fpsAccumulatedFrames = 0;
  PauseManager::init(nowSeconds);
  resetPerfTrackingState();
}

void UPDATE(double nowSeconds, float dtSeconds) {
  if (!initialized) {
    INIT(nowSeconds);
  }

  nowSecondsCached = nowSeconds;
  deltaTimeSeconds = std::max(0.0f, dtSeconds);
  PauseManager::update(nowSeconds, deltaTimeSeconds);
  frameCount++;

  if (deltaTimeSeconds > 0.0f) {
    fpsAccumulatedSeconds += static_cast<double>(deltaTimeSeconds);
    fpsAccumulatedFrames++;

    if (fpsAccumulatedSeconds >= fpsSampleIntervalSeconds) {
      fpsCached = static_cast<float>(static_cast<double>(fpsAccumulatedFrames) /
                                     fpsAccumulatedSeconds);
      fpsAccumulatedSeconds = 0.0;
      fpsAccumulatedFrames = 0;
    }
  }

  syncWindowState();
}

float GETFPS() { return fpsCached; }
float GETDELTATIME() { return PauseManager::gameDeltaTime(); }
float GETFRAMETIMEMS() { return deltaTimeSeconds * 1000.0f; }
double GETTIME() { return PauseManager::gameTime(); }
double GETTIMESINCESTART() { return PauseManager::gameTimeSinceStart(); }
std::uint64_t GETFRAMECOUNT() { return frameCount; }
float GETCPUFRAMETIMEMS() { return cpuFrameMetric.displayMs; }
float GETGPUFRAMETIMEMS() { return gpuFrameMetric.displayMs; }
float GETCHUNKRENDERTIMEMS() { return chunkRenderMetric.displayMs; }
int GETRENDEREDCHUNKCOUNT() { return renderedChunkCountCached; }

void BEGINPERFFRAME() {
  frameChunkRenderTotalMs = 0.0f;
  frameChunkRenderCount = 0;
  resolveGpuQueries();
}

void BEGINGPUFRAMEQUERY() {
  ensureGpuQueries();
  if (!gpuQueriesInitialized || gpuQueryInFlight) {
    return;
  }

  glBeginQuery(GL_TIME_ELAPSED, gpuQueries[gpuQueryWriteIndex]);
  gpuQueryInFlight = true;
}

void ENDGPUFRAMEQUERY() {
  if (!gpuQueryInFlight || !gpuQueriesInitialized) {
    return;
  }

  glEndQuery(GL_TIME_ELAPSED);
  gpuQueryPending[gpuQueryWriteIndex] = true;
  gpuQueryWriteIndex = (gpuQueryWriteIndex + 1) % gpuQueries.size();
  gpuQueryInFlight = false;
}

void ACCUMULATECHUNKRENDER(float totalMs, int chunkCount) {
  frameChunkRenderTotalMs += std::max(totalMs, 0.0f);
  frameChunkRenderCount += std::max(chunkCount, 0);
}

void ENDPERFFRAME(float cpuFrameMs, float fpsInstant) {
  updateMetric(cpuFrameMetric, cpuFrameMs);

  renderedChunkCountCached = frameChunkRenderCount;
  const float averageChunkRenderMs =
      frameChunkRenderCount > 0
          ? (frameChunkRenderTotalMs / static_cast<float>(frameChunkRenderCount))
          : 0.0f;
  updateMetric(chunkRenderMetric, averageChunkRenderMs);

  //finalizeFpsSpikeIfNeeded(fpsInstant);
}

void CLEANUPPERF() {
  if (gpuQueriesInitialized) {
    if (gpuQueryInFlight) {
      glEndQuery(GL_TIME_ELAPSED);
      gpuQueryInFlight = false;
    }
    glDeleteQueries(static_cast<GLsizei>(gpuQueries.size()), gpuQueries.data());
    gpuQueries.fill(0);
    gpuQueriesInitialized = false;
  }
  gpuQueryPending.fill(false);
}

void SETFPSSAMPLEINTERVAL(double seconds) {
  if (seconds <= 0.0) {
    return;
  }
  fpsSampleIntervalSeconds = seconds;
  fpsAccumulatedSeconds = 0.0;
  fpsAccumulatedFrames = 0;
}

double GETFPSSAMPLEINTERVAL() { return fpsSampleIntervalSeconds; }

bool ISPAUSED() { return PauseManager::isPaused(); }

void SETPAUSED(bool v) { PauseManager::setPaused(v); }

void TOGGLEPAUSED() { PauseManager::togglePaused(); }

std::uint64_t ADDPAUSELISTENER(PauseListener listener) {
  return PauseManager::addListener(std::move(listener));
}

void REMPAUSELISTENER(std::uint64_t id) { PauseManager::removeListener(id); }

glm::vec2 GETDEFAULTVIEWPORTSIZE() { return defaultViewportSize; }
glm::vec2 GETVIEWPORTSIZE() { return viewportSize; }
VIEWPORTMODE GETVIEWPORTMODE() { return viewportMode; }
glm::vec2 GETMONITORSIZE() {
  refreshMonitorSize();
  return monitorSize;
}
glm::vec2 GETWINDOWPOS() { return windowPos; }
bool ISVSYNCENABLED() { return vSyncEnabled; }

void SETDEFAULTVIEWPORTSIZE(const glm::vec2& size) {
  if (size.x <= 0.0f || size.y <= 0.0f) return;
  defaultViewportSize = size;
  if (!attachedWindow) {
    viewportSize = size;
  }
}

void SETVIEWPORTSIZE(const glm::vec2& size) {
  if (size.x <= 0.0f || size.y <= 0.0f) return;

  viewportSize = size;
  if (viewportMode == VIEWPORTMODE::WINDOWMODE) {
    lastWindowedSize = size;
  }

  if (!attachedWindow) return;

  if (viewportMode == VIEWPORTMODE::FULLSCREEN) {
    applyWindowMode();
  } else {
    const int windowWidth = toInt(size.x, 1280);
    const int windowHeight = toInt(size.y, 720);
    glfwSetWindowSize(attachedWindow, windowWidth, windowHeight);

    if (viewportMode == VIEWPORTMODE::WINDOWMODE) {
      GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
      const GLFWvidmode* videoMode =
          primaryMonitor ? glfwGetVideoMode(primaryMonitor) : nullptr;
      if (primaryMonitor && videoMode) {
        const glm::ivec2 centeredPos = resolveCenteredWindowPos(
            primaryMonitor, videoMode, glm::ivec2(windowWidth, windowHeight));
        glfwSetWindowPos(attachedWindow, centeredPos.x, centeredPos.y);
      }
    }

    syncWindowState();
  }
}

void SETVIEWPORTMODE(VIEWPORTMODE mode) {
  if (viewportMode == mode) return;

  if (attachedWindow && viewportMode == VIEWPORTMODE::WINDOWMODE) {
    syncWindowState();
  }

  viewportMode = mode;
  applyWindowMode();
}

void SETVSYNC(bool enabled) {
  vSyncEnabled = enabled;
  applyVSync();
}

void ATTACHWINDOW(GLFWwindow* window) {
  attachedWindow = window;
  refreshMonitorSize();
  if (!attachedWindow) return;

  glfwSetFramebufferSizeCallback(attachedWindow, framebufferSizeCallback);
  glfwSetWindowPosCallback(attachedWindow, windowPosCallback);
  syncWindowState();
  applyWindowMode();
  applyVSync();
}

void SHUTDOWN() {
  CLEANUPPERF();
  PauseManager::shutdown();

  initialized = false;
  nowSecondsCached = 0.0;
  deltaTimeSeconds = 0.0f;
  frameCount = 0;
  fpsCached = 0.0f;
  fpsSampleIntervalSeconds = 1.0;
  fpsAccumulatedSeconds = 0.0;
  fpsAccumulatedFrames = 0;

  attachedWindow = nullptr;
  defaultViewportSize = glm::vec2(1280.0f, 720.0f);
  viewportSize = defaultViewportSize;
  monitorSize = glm::vec2(0.0f);
  windowPos = glm::vec2(0.0f);
  lastWindowedSize = defaultViewportSize;
  lastWindowedPos = glm::vec2(100.0f, 100.0f);
  viewportMode = VIEWPORTMODE::WINDOWMODE;
  vSyncEnabled = false;

  resetPerfTrackingState();
}

} // namespace ENGINE
