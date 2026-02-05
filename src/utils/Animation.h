#pragma once

#include <windows.h>
#include <cmath>
#include <functional>

namespace VirtualOverlay {

// Check if Windows "reduce motion" accessibility is enabled
// or if Windows animations are disabled
bool ShouldReduceMotion();

// Easing function types
enum class EaseType {
    Linear,
    EaseInQuad,
    EaseOutQuad,
    EaseInOutQuad,
    EaseInCubic,
    EaseOutCubic,
    EaseInOutCubic,
    EaseInExpo,
    EaseOutExpo,
    EaseInOutExpo,
    EaseInBack,
    EaseOutBack,
    EaseInOutBack
};

// Animation easing functions
// All functions take t in [0, 1] and return value in [0, 1]
class Easing {
public:
    // Get easing function by type
    static std::function<float(float)> Get(EaseType type);

    // Individual easing functions
    static float Linear(float t);
    static float EaseInQuad(float t);
    static float EaseOutQuad(float t);
    static float EaseInOutQuad(float t);
    static float EaseInCubic(float t);
    static float EaseOutCubic(float t);
    static float EaseInOutCubic(float t);
    static float EaseInExpo(float t);
    static float EaseOutExpo(float t);
    static float EaseInOutExpo(float t);
    static float EaseInBack(float t);
    static float EaseOutBack(float t);
    static float EaseInOutBack(float t);
};

// Simple interpolation helper
class Interpolator {
public:
    Interpolator() = default;
    Interpolator(float start, float end, float duration, EaseType ease = EaseType::EaseOutQuad);

    // Reset animation
    void Reset(float start, float end, float duration, EaseType ease = EaseType::EaseOutQuad);

    // Update animation with delta time (in seconds)
    void Update(float deltaTime);

    // Get current interpolated value
    float GetValue() const;

    // Check if animation is complete
    bool IsComplete() const;

    // Get progress [0, 1]
    float GetProgress() const;

    // Snap to end value
    void Complete();

private:
    float m_start = 0.0f;
    float m_end = 0.0f;
    float m_duration = 0.0f;
    float m_elapsed = 0.0f;
    float m_current = 0.0f;
    EaseType m_easeType = EaseType::EaseOutQuad;
};

// Smooth value tracker (for smoothing factor approach)
class SmoothValue {
public:
    SmoothValue() = default;
    explicit SmoothValue(float initial, float smoothing = 0.15f);

    // Set target value
    void SetTarget(float target);

    // Set current value immediately (no smoothing)
    void SetImmediate(float value);

    // Update with delta time (in seconds)
    void Update(float deltaTime);

    // Get current smoothed value
    float GetValue() const;

    // Get target value
    float GetTarget() const;

    // Check if value has reached target (within epsilon)
    bool HasReachedTarget(float epsilon = 0.001f) const;

    // Set smoothing factor (0 = instant, higher = slower)
    void SetSmoothing(float smoothing);

private:
    float m_current = 0.0f;
    float m_target = 0.0f;
    float m_smoothing = 0.15f;
};

}  // namespace VirtualOverlay
