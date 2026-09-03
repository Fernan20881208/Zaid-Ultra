#include "RunPredictor.hpp"

#include <algorithm>
#include <cmath>

namespace zaidultra {

RunPredictor& RunPredictor::get() {
    static RunPredictor instance;
    return instance;
}

void RunPredictor::resetLevel() {
    m_deaths.clear();
    m_current = 0.f;
    m_best = 0.f;
    m_attempts = 0;
    m_completions = 0;
}

void RunPredictor::startAttempt() {
    m_current = 0.f;
    ++m_attempts;
}

void RunPredictor::observeProgress(float percent) {
    m_current = std::max(m_current, std::clamp(percent, 0.f, 100.f));
}

void RunPredictor::recordDeath(float percent) {
    const float deathPercent = std::clamp(std::max(percent, m_current), 0.f, 100.f);
    m_current = deathPercent;
    m_best = std::max(m_best, deathPercent);
    m_deaths.push_back(deathPercent);
    while (m_deaths.size() > kHistoryLimit) {
        m_deaths.pop_front();
    }
}

void RunPredictor::recordCompletion() {
    m_current = 100.f;
    m_best = 100.f;
    ++m_completions;
}

RunPrediction RunPredictor::prediction() const {
    RunPrediction out;
    out.currentPercent = m_current;
    out.sessionBest = m_best;
    out.attempts = m_attempts;

    if (m_current >= 100.f) {
        out.pbMomentum = 100.f;
        out.completionConfidence = 100.f;
        return out;
    }

    const float baseProgress = std::clamp(m_current, 0.f, 100.f);

    std::size_t reachedCurrent = 0;
    std::size_t reachedNextBand = 0;
    const float nextBand = std::min(100.f, std::max(m_current + 10.f, m_best));
    for (float death : m_deaths) {
        if (death + 0.01f >= m_current) {
            ++reachedCurrent;
        }
        if (death + 0.01f >= nextBand) {
            ++reachedNextBand;
        }
    }

    float historicalCarry = 0.5f;
    if (reachedCurrent > 0) {
        historicalCarry = static_cast<float>(reachedNextBand) / static_cast<float>(reachedCurrent);
    }

    const float bestPressure = m_best > 0.f ? std::clamp(m_current / m_best, 0.f, 1.25f) : 0.f;
    out.pbMomentum = std::clamp(
        baseProgress * 0.45f + historicalCarry * 35.f + bestPressure * 20.f,
        0.f,
        100.f
    );

    const float completionPrior = m_attempts > 0
        ? static_cast<float>(m_completions) / static_cast<float>(m_attempts)
        : 0.f;
    const float progressCurve = std::pow(baseProgress / 100.f, 2.15f);
    out.completionConfidence = std::clamp(
        progressCurve * 75.f + historicalCarry * 20.f + completionPrior * 100.f * 0.05f,
        0.f,
        100.f
    );

    return out;
}

} // namespace zaidultra
