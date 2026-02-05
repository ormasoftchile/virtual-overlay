#include "Animation.h"
#include <algorithm>

namespace VirtualOverlay {

constexpr float PI = 3.14159265358979323846f;

bool ShouldReduceMotion() {
    // Check SPI_GETCLIENTAREAANIMATION - indicates if animations should be shown
    BOOL animationsEnabled = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animationsEnabled, 0);
    if (!animationsEnabled) {
        return true;
    }

    // Check SPI_GETMENUFADE - indicates if menu animations are enabled
    BOOL menuFade = TRUE;
    SystemParametersInfoW(SPI_GETMENUFADE, 0, &menuFade, 0);
    
    // Check SPI_GETMENUANIMATION
    BOOL menuAnimation = TRUE;
    SystemParametersInfoW(SPI_GETMENUANIMATION, 0, &menuAnimation, 0);

    // If both menu features are disabled, user likely prefers reduced motion
    if (!menuFade && !menuAnimation) {
        return true;
    }

    return false;
}

std::function<float(float)> Easing::Get(EaseType type) {
    switch (type) {
        case EaseType::Linear:        return Linear;
        case EaseType::EaseInQuad:    return EaseInQuad;
        case EaseType::EaseOutQuad:   return EaseOutQuad;
        case EaseType::EaseInOutQuad: return EaseInOutQuad;
        case EaseType::EaseInCubic:   return EaseInCubic;
        case EaseType::EaseOutCubic:  return EaseOutCubic;
        case EaseType::EaseInOutCubic:return EaseInOutCubic;
        case EaseType::EaseInExpo:    return EaseInExpo;
        case EaseType::EaseOutExpo:   return EaseOutExpo;
        case EaseType::EaseInOutExpo: return EaseInOutExpo;
        case EaseType::EaseInBack:    return EaseInBack;
        case EaseType::EaseOutBack:   return EaseOutBack;
        case EaseType::EaseInOutBack: return EaseInOutBack;
        default:                      return Linear;
    }
}

float Easing::Linear(float t) {
    return t;
}

float Easing::EaseInQuad(float t) {
    return t * t;
}

float Easing::EaseOutQuad(float t) {
    return t * (2.0f - t);
}

float Easing::EaseInOutQuad(float t) {
    return t < 0.5f 
        ? 2.0f * t * t 
        : -1.0f + (4.0f - 2.0f * t) * t;
}

float Easing::EaseInCubic(float t) {
    return t * t * t;
}

float Easing::EaseOutCubic(float t) {
    float f = t - 1.0f;
    return f * f * f + 1.0f;
}

float Easing::EaseInOutCubic(float t) {
    return t < 0.5f
        ? 4.0f * t * t * t
        : (t - 1.0f) * (2.0f * t - 2.0f) * (2.0f * t - 2.0f) + 1.0f;
}

float Easing::EaseInExpo(float t) {
    return t == 0.0f ? 0.0f : std::pow(2.0f, 10.0f * (t - 1.0f));
}

float Easing::EaseOutExpo(float t) {
    return t == 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
}

float Easing::EaseInOutExpo(float t) {
    if (t == 0.0f) return 0.0f;
    if (t == 1.0f) return 1.0f;
    if (t < 0.5f) {
        return std::pow(2.0f, 20.0f * t - 10.0f) / 2.0f;
    }
    return (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) / 2.0f;
}

float Easing::EaseInBack(float t) {
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.0f;
    return c3 * t * t * t - c1 * t * t;
}

float Easing::EaseOutBack(float t) {
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.0f;
    float f = t - 1.0f;
    return 1.0f + c3 * f * f * f + c1 * f * f;
}

float Easing::EaseInOutBack(float t) {
    constexpr float c1 = 1.70158f;
    constexpr float c2 = c1 * 1.525f;
    if (t < 0.5f) {
        return (std::pow(2.0f * t, 2.0f) * ((c2 + 1.0f) * 2.0f * t - c2)) / 2.0f;
    }
    return (std::pow(2.0f * t - 2.0f, 2.0f) * ((c2 + 1.0f) * (t * 2.0f - 2.0f) + c2) + 2.0f) / 2.0f;
}

// Interpolator implementation
Interpolator::Interpolator(float start, float end, float duration, EaseType ease)
    : m_start(start), m_end(end), m_duration(duration), m_elapsed(0.0f), 
      m_current(start), m_easeType(ease) {
}

void Interpolator::Reset(float start, float end, float duration, EaseType ease) {
    m_start = start;
    m_end = end;
    m_duration = duration;
    m_elapsed = 0.0f;
    m_current = start;
    m_easeType = ease;
}

void Interpolator::Update(float deltaTime) {
    if (IsComplete()) return;

    m_elapsed += deltaTime;
    float progress = GetProgress();
    
    auto easeFn = Easing::Get(m_easeType);
    float easedProgress = easeFn(progress);
    
    m_current = m_start + (m_end - m_start) * easedProgress;
}

float Interpolator::GetValue() const {
    return m_current;
}

bool Interpolator::IsComplete() const {
    return m_duration <= 0.0f || m_elapsed >= m_duration;
}

float Interpolator::GetProgress() const {
    if (m_duration <= 0.0f) return 1.0f;
    return std::clamp(m_elapsed / m_duration, 0.0f, 1.0f);
}

void Interpolator::Complete() {
    m_elapsed = m_duration;
    m_current = m_end;
}

// SmoothValue implementation
SmoothValue::SmoothValue(float initial, float smoothing)
    : m_current(initial), m_target(initial), m_smoothing(smoothing) {
}

void SmoothValue::SetTarget(float target) {
    m_target = target;
}

void SmoothValue::SetImmediate(float value) {
    m_current = value;
    m_target = value;
}

void SmoothValue::Update(float deltaTime) {
    // Exponential smoothing: current += (target - current) * (1 - e^(-dt/smoothing))
    // For small smoothing values, this approximates: current += (target - current) * dt / smoothing
    if (m_smoothing <= 0.0f) {
        m_current = m_target;
        return;
    }
    
    float factor = 1.0f - std::exp(-deltaTime / m_smoothing);
    m_current += (m_target - m_current) * factor;
}

float SmoothValue::GetValue() const {
    return m_current;
}

float SmoothValue::GetTarget() const {
    return m_target;
}

bool SmoothValue::HasReachedTarget(float epsilon) const {
    return std::abs(m_target - m_current) < epsilon;
}

void SmoothValue::SetSmoothing(float smoothing) {
    m_smoothing = smoothing;
}

}  // namespace VirtualOverlay
