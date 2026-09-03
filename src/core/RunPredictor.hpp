#pragma once

#include <cstddef>
#include <deque>

namespace zaidultra {

struct RunPrediction {
    float currentPercent = 0.f;
    float sessionBest = 0.f;
    float pbMomentum = 0.f;
    float completionConfidence = 0.f;
    std::size_t attempts = 0;
};

class RunPredictor final {
public:
    static RunPredictor& get();

    void resetLevel();
    void startAttempt();
    void observeProgress(float percent);
    void recordDeath(float percent);
    void recordCompletion();

    [[nodiscard]] RunPrediction prediction() const;

private:
    std::deque<float> m_deaths;
    float m_current = 0.f;
    float m_best = 0.f;
    std::size_t m_attempts = 0;
    std::size_t m_completions = 0;
    static constexpr std::size_t kHistoryLimit = 600;
};

} // namespace zaidultra
