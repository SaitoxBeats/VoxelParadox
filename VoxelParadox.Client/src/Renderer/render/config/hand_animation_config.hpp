// File: VoxelParadox.Client/src/Renderer/render/config/hand_animation_config.hpp
// Purpose: procedural hand animation state and tuning for break/place actions.
// Flow: BlockInteractionSystem fires triggers, the renderer reads animation state each frame.

#pragma once

// 1. Standard Library
#include <algorithm>
#include <cmath>

// 2. Third-party Libraries
#include <glm/glm.hpp>

// --- Easing ---

enum class EasingType {
    Linear,
    EaseInSine, EaseOutSine, EaseInOutSine,
    EaseInQuad, EaseOutQuad, EaseInOutQuad,
    EaseInCubic, EaseOutCubic, EaseInOutCubic,
    EaseInQuart, EaseOutQuart, EaseInOutQuart,
    EaseInQuint, EaseOutQuint, EaseInOutQuint,
    EaseInExpo, EaseOutExpo, EaseInOutExpo,
    EaseInCirc, EaseOutCirc, EaseInOutCirc,
    EaseInBack, EaseOutBack, EaseInOutBack,
    EaseInElastic, EaseOutElastic, EaseInOutElastic,
    EaseInBounce, EaseOutBounce, EaseInOutBounce,
    COUNT
};

inline constexpr int kEasingTypeCount = static_cast<int>(EasingType::COUNT);

inline const char* getEasingName(EasingType type) {
    switch (type) {
        case EasingType::Linear:         return "Linear";
        case EasingType::EaseInSine:     return "easeInSine";
        case EasingType::EaseOutSine:    return "easeOutSine";
        case EasingType::EaseInOutSine:  return "easeInOutSine";
        case EasingType::EaseInQuad:     return "easeInQuad";
        case EasingType::EaseOutQuad:    return "easeOutQuad";
        case EasingType::EaseInOutQuad:  return "easeInOutQuad";
        case EasingType::EaseInCubic:    return "easeInCubic";
        case EasingType::EaseOutCubic:   return "easeOutCubic";
        case EasingType::EaseInOutCubic: return "easeInOutCubic";
        case EasingType::EaseInQuart:    return "easeInQuart";
        case EasingType::EaseOutQuart:   return "easeOutQuart";
        case EasingType::EaseInOutQuart: return "easeInOutQuart";
        case EasingType::EaseInQuint:    return "easeInQuint";
        case EasingType::EaseOutQuint:   return "easeOutQuint";
        case EasingType::EaseInOutQuint: return "easeInOutQuint";
        case EasingType::EaseInExpo:     return "easeInExpo";
        case EasingType::EaseOutExpo:    return "easeOutExpo";
        case EasingType::EaseInOutExpo:  return "easeInOutExpo";
        case EasingType::EaseInCirc:     return "easeInCirc";
        case EasingType::EaseOutCirc:    return "easeOutCirc";
        case EasingType::EaseInOutCirc:  return "easeInOutCirc";
        case EasingType::EaseInBack:     return "easeInBack";
        case EasingType::EaseOutBack:    return "easeOutBack";
        case EasingType::EaseInOutBack:  return "easeInOutBack";
        case EasingType::EaseInElastic:  return "easeInElastic";
        case EasingType::EaseOutElastic: return "easeOutElastic";
        case EasingType::EaseInOutElastic: return "easeInOutElastic";
        case EasingType::EaseInBounce:   return "easeInBounce";
        case EasingType::EaseOutBounce:  return "easeOutBounce";
        case EasingType::EaseInOutBounce: return "easeInOutBounce";
        default:                         return "Linear";
    }
}

namespace EasingDetail {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kHalfPi = kPi * 0.5f;
constexpr float kTwoPi = kPi * 2.0f;
constexpr float kBackC1 = 1.70158f;
constexpr float kBackC2 = kBackC1 * 1.525f;
constexpr float kBackC3 = kBackC1 + 1.0f;
constexpr float kElasticC4 = kTwoPi / 3.0f;
constexpr float kElasticC5 = kTwoPi / 4.5f;

inline float bounceOut(float t) {
    if (t < 1.0f / 2.75f) {
        return 7.5625f * t * t;
    }
    if (t < 2.0f / 2.75f) {
        t -= 1.5f / 2.75f;
        return 7.5625f * t * t + 0.75f;
    }
    if (t < 2.5f / 2.75f) {
        t -= 2.25f / 2.75f;
        return 7.5625f * t * t + 0.9375f;
    }
    t -= 2.625f / 2.75f;
    return 7.5625f * t * t + 0.984375f;
}

}  // namespace EasingDetail

inline float evaluateEasing(EasingType type, float t) {
    using namespace EasingDetail;
    t = glm::clamp(t, 0.0f, 1.0f);

    switch (type) {
        case EasingType::Linear:
            return t;

        // --- Sine ---
        case EasingType::EaseInSine:
            return 1.0f - std::cos(t * kHalfPi);
        case EasingType::EaseOutSine:
            return std::sin(t * kHalfPi);
        case EasingType::EaseInOutSine:
            return -(std::cos(kPi * t) - 1.0f) * 0.5f;

        // --- Quad ---
        case EasingType::EaseInQuad:
            return t * t;
        case EasingType::EaseOutQuad:
            return 1.0f - (1.0f - t) * (1.0f - t);
        case EasingType::EaseInOutQuad:
            return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) * 0.5f;

        // --- Cubic ---
        case EasingType::EaseInCubic:
            return t * t * t;
        case EasingType::EaseOutCubic: {
            const float u = 1.0f - t;
            return 1.0f - u * u * u;
        }
        case EasingType::EaseInOutCubic:
            return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;

        // --- Quart ---
        case EasingType::EaseInQuart:
            return t * t * t * t;
        case EasingType::EaseOutQuart: {
            const float u = 1.0f - t;
            return 1.0f - u * u * u * u;
        }
        case EasingType::EaseInOutQuart:
            return t < 0.5f ? 8.0f * t * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 4.0f) * 0.5f;

        // --- Quint ---
        case EasingType::EaseInQuint:
            return t * t * t * t * t;
        case EasingType::EaseOutQuint: {
            const float u = 1.0f - t;
            return 1.0f - u * u * u * u * u;
        }
        case EasingType::EaseInOutQuint:
            return t < 0.5f ? 16.0f * t * t * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 5.0f) * 0.5f;

        // --- Expo ---
        case EasingType::EaseInExpo:
            return t <= 0.0f ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f);
        case EasingType::EaseOutExpo:
            return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
        case EasingType::EaseInOutExpo:
            if (t <= 0.0f) return 0.0f;
            if (t >= 1.0f) return 1.0f;
            return t < 0.5f
                ? std::pow(2.0f, 20.0f * t - 10.0f) * 0.5f
                : (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) * 0.5f;

        // --- Circ ---
        case EasingType::EaseInCirc:
            return 1.0f - std::sqrt(1.0f - t * t);
        case EasingType::EaseOutCirc:
            return std::sqrt(1.0f - (t - 1.0f) * (t - 1.0f));
        case EasingType::EaseInOutCirc:
            return t < 0.5f
                ? (1.0f - std::sqrt(1.0f - 4.0f * t * t)) * 0.5f
                : (std::sqrt(1.0f - std::pow(-2.0f * t + 2.0f, 2.0f)) + 1.0f) * 0.5f;

        // --- Back ---
        case EasingType::EaseInBack:
            return kBackC3 * t * t * t - kBackC1 * t * t;
        case EasingType::EaseOutBack: {
            const float u = t - 1.0f;
            return 1.0f + kBackC3 * u * u * u + kBackC1 * u * u;
        }
        case EasingType::EaseInOutBack:
            return t < 0.5f
                ? (std::pow(2.0f * t, 2.0f) * ((kBackC2 + 1.0f) * 2.0f * t - kBackC2)) * 0.5f
                : (std::pow(2.0f * t - 2.0f, 2.0f) * ((kBackC2 + 1.0f) * (2.0f * t - 2.0f) + kBackC2) + 2.0f) * 0.5f;

        // --- Elastic ---
        case EasingType::EaseInElastic:
            if (t <= 0.0f) return 0.0f;
            if (t >= 1.0f) return 1.0f;
            return -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * kElasticC4);
        case EasingType::EaseOutElastic:
            if (t <= 0.0f) return 0.0f;
            if (t >= 1.0f) return 1.0f;
            return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * kElasticC4) + 1.0f;
        case EasingType::EaseInOutElastic:
            if (t <= 0.0f) return 0.0f;
            if (t >= 1.0f) return 1.0f;
            return t < 0.5f
                ? -(std::pow(2.0f, 20.0f * t - 10.0f) * std::sin((20.0f * t - 11.125f) * kElasticC5)) * 0.5f
                : (std::pow(2.0f, -20.0f * t + 10.0f) * std::sin((20.0f * t - 11.125f) * kElasticC5)) * 0.5f + 1.0f;

        // --- Bounce ---
        case EasingType::EaseInBounce:
            return 1.0f - bounceOut(1.0f - t);
        case EasingType::EaseOutBounce:
            return bounceOut(t);
        case EasingType::EaseInOutBounce:
            return t < 0.5f
                ? (1.0f - bounceOut(1.0f - 2.0f * t)) * 0.5f
                : (1.0f + bounceOut(2.0f * t - 1.0f)) * 0.5f;

        default:
            return t;
    }
}

// --- Animation Config ---

struct HandAnimationConfig {
    // --- Break (swing) animation ---
    float swingSpeed = 3.0f;
    glm::vec3 swingRotationDegrees{ -40.0f, 0.0f, -17.0f };
    glm::vec3 swingTranslation{ 0.085f, 0.0f, -0.120f };
    float swingIntensity = 1.0f;
    EasingType swingEaseIn = EasingType::EaseOutBack;
    EasingType swingEaseOut = EasingType::EaseInOutQuad;

    // --- Place (punch) animation ---
    float punchSpeed = 3.5f;
    glm::vec3 punchRotationDegrees{ -14.5f, 0.0f, 24.0f };
    glm::vec3 punchTranslation{ 0.10f, 0.0f, 0.0f };
    float punchIntensity = 1.0f;
    EasingType punchEaseIn = EasingType::EaseOutCubic;
    EasingType punchEaseOut = EasingType::EaseInOutQuart;

    // --- Place hold delay ---
    float placeHoldDelay = 0.16f;

    // --- Use (utility/food) animation ---
    float useSpeed = 4.0f;
    glm::vec3 useRotationDegrees{ 00.0f, 0.0f, 0.0f };
    glm::vec3 useTranslation{ 0.0f, 0.0f, -0.30f };
    float useIntensity = 1.0f;
    EasingType useEaseIn = EasingType::EaseInOutSine;
    EasingType useEaseOut = EasingType::EaseInOutSine;

    // --- Idle sway ---
    float idleSwaySpeed = 3.2f;
    float idleSwayAmount = 0.012f;
};

// --- Animation State ---

struct HandAnimationState {
    enum class AnimationType { NONE, SWING, PUNCH, USE };

    AnimationType activeAnimation = AnimationType::NONE;
    float timer = 0.0f;
    float duration = 0.0f;
    float progress = 0.0f;

    // Place hold: keeps the placed item visible in hand for a short time.
    bool placeHoldActive = false;
    float placeHoldTimer = 0.0f;

    bool isPlaying() const { return activeAnimation != AnimationType::NONE && timer < duration; }
    bool isPlaceHolding() const { return placeHoldActive; }

    void triggerSwing(float speed) {
        activeAnimation = AnimationType::SWING;
        timer = 0.0f;
        duration = (speed > 0.0f) ? (1.0f / speed) : 0.08f;
        progress = 0.0f;
    }

    void triggerPunch(float speed, float holdDelay) {
        activeAnimation = AnimationType::PUNCH;
        timer = 0.0f;
        duration = (speed > 0.0f) ? (1.0f / speed) : 0.07f;
        progress = 0.0f;
        placeHoldActive = holdDelay > 0.0f;
        placeHoldTimer = holdDelay;
    }

    void triggerUse(float speed) {
        activeAnimation = AnimationType::USE;
        timer = 0.0f;
        duration = (speed > 0.0f) ? (1.0f / speed) : 0.08f;
        progress = 0.0f;
    }

    void update(float dt, const HandAnimationConfig& config) {
        // Tick place hold
        if (placeHoldActive) {
            placeHoldTimer -= dt;
            if (placeHoldTimer <= 0.0f) {
                placeHoldActive = false;
                placeHoldTimer = 0.0f;
            }
        }

        if (activeAnimation == AnimationType::NONE) {
            return;
        }

        timer += dt;
        if (timer >= duration) {
            activeAnimation = AnimationType::NONE;
            timer = 0.0f;
            progress = 0.0f;
            return;
        }

        const float t = timer / duration;

        EasingType easeIn = EasingType::Linear;
        EasingType easeOut = EasingType::Linear;
        float intensity = 1.0f;
        if (activeAnimation == AnimationType::SWING) {
            easeIn = config.swingEaseIn;
            easeOut = config.swingEaseOut;
            intensity = config.swingIntensity;
        } else if (activeAnimation == AnimationType::PUNCH) {
            easeIn = config.punchEaseIn;
            easeOut = config.punchEaseOut;
            intensity = config.punchIntensity;
        } else if (activeAnimation == AnimationType::USE) {
            easeIn = config.useEaseIn;
            easeOut = config.useEaseOut;
            intensity = config.useIntensity;
        }

        // First half: incoming (0->1), second half: outgoing (1->0)
        float raw;
        if (t < 0.5f) {
            const float phase = t * 2.0f;
            raw = evaluateEasing(easeIn, phase);
        } else {
            const float phase = (t - 0.5f) * 2.0f;
            raw = 1.0f - evaluateEasing(easeOut, phase);
        }

        progress = glm::clamp(raw * intensity, -2.0f, 2.0f);
    }

    glm::vec3 getRotationOffsetDegrees(const HandAnimationConfig& config) const {
        if (activeAnimation == AnimationType::SWING) {
            return config.swingRotationDegrees * progress;
        }
        if (activeAnimation == AnimationType::PUNCH) {
            return config.punchRotationDegrees * progress;
        }
        if (activeAnimation == AnimationType::USE) {
            return config.useRotationDegrees * progress;
        }
        return glm::vec3(0.0f);
    }

    glm::vec3 getTranslationOffset(const HandAnimationConfig& config) const {
        if (activeAnimation == AnimationType::SWING) {
            return config.swingTranslation * progress;
        }
        if (activeAnimation == AnimationType::PUNCH) {
            return config.punchTranslation * progress;
        }
        if (activeAnimation == AnimationType::USE) {
            return config.useTranslation * progress;
        }
        return glm::vec3(0.0f);
    }
};

namespace HandAnimation {
    inline HandAnimationConfig config{};
    inline HandAnimationState state{};
}
