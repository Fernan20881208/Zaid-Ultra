#include "SmartDetail.hpp"

#include <algorithm>
#include <cmath>

namespace zaidultra {

SmartDetail& SmartDetail::get() {
    static SmartDetail instance;
    return instance;
}

std::size_t SmartDetail::bucketFor(float percent) {
    return static_cast<std::size_t>(std::clamp(std::lround(percent), 0L, 100L));
}

void SmartDetail::resetSession() {
    m_buckets = {};
    m_activeTier = DetailTier::Normal;
    m_upgradeEvidence = 0;
    m_recoveryEvidence = 0;
}

void SmartDetail::observe(float percent, float frameMs, float targetFps) {
    if (!std::isfinite(frameMs) || frameMs <= 0.f || frameMs > 1000.f || targetFps <= 0.f) {
        return;
    }

    auto& bucket = m_buckets[bucketFor(percent)];
    constexpr float alpha = 0.08f;
    bucket.emaFrameMs = bucket.samples == 0
        ? frameMs
        : bucket.emaFrameMs * (1.f - alpha) + frameMs * alpha;
    ++bucket.samples;

    const auto wanted = decision(percent, targetFps).tier;
    const int wantedValue = static_cast<int>(wanted);
    const int currentValue = static_cast<int>(m_activeTier);

    if (wantedValue > currentValue) {
        ++m_upgradeEvidence;
        m_recoveryEvidence = 0;
        if (m_upgradeEvidence >= 18) {
            m_activeTier = wanted;
            m_upgradeEvidence = 0;
        }
    } else if (wantedValue < currentValue) {
        ++m_recoveryEvidence;
        m_upgradeEvidence = 0;
        if (m_recoveryEvidence >= 120) {
            m_activeTier = wanted;
            m_recoveryEvidence = 0;
        }
    } else {
        m_upgradeEvidence = 0;
        m_recoveryEvidence = 0;
    }
}

DetailDecision SmartDetail::decision(float percent, float targetFps) const {
    DetailDecision out;
    if (targetFps <= 0.f) {
        return out;
    }

    out.budgetMs = 1000.f / targetFps;

    const auto index = bucketFor(percent);
    float weightedMs = 0.f;
    float weightSum = 0.f;
    std::size_t totalSamples = 0;

    for (int delta = -2; delta <= 2; ++delta) {
        const int raw = static_cast<int>(index) + delta;
        if (raw < 0 || raw > 100) {
            continue;
        }

        const auto& bucket = m_buckets[static_cast<std::size_t>(raw)];
        if (bucket.samples == 0) {
            continue;
        }

        const float weight = delta == 0 ? 1.f : (std::abs(delta) == 1 ? 0.65f : 0.35f);
        weightedMs += bucket.emaFrameMs * weight;
        weightSum += weight;
        totalSamples += bucket.samples;
    }

    if (weightSum <= 0.f) {
        return out;
    }

    out.learnedFrameMs = weightedMs / weightSum;
    out.confidence = std::clamp(static_cast<float>(totalSamples) / 240.f, 0.f, 1.f);

    if (out.confidence < 0.08f) {
        out.tier = DetailTier::Normal;
        return out;
    }

    const float ratio = out.learnedFrameMs / out.budgetMs;
    if (ratio >= 1.45f) {
        out.tier = DetailTier::ULDM;
    } else if (ratio >= 1.12f) {
        out.tier = DetailTier::LDM;
    } else {
        out.tier = DetailTier::Normal;
    }

    return out;
}

DetailTier SmartDetail::activeTier() const {
    return m_activeTier;
}

} // namespace zaidultra
