#pragma once

#include <cstddef>
#include <deque>

namespace zaidultra {

struct FrameSnapshot {
    float averageFrameMs = 0.f;
    float worstFrameMs = 0.f;
    float onePercentLowFps = 0.f;
    float framePacingScore = 100.f;
    int spikeCount = 0;
};

class Telemetry final {
public:
    static Telemetry& get();

    void resetAttempt();
    void sample(float dt, float percent);

    [[nodiscard]] FrameSnapshot snapshot(std::size_t window = 240) const;
    [[nodiscard]] float performanceScore(float targetFps) const;
    [[nodiscard]] float currentPercent() const;

private:
    struct Sample {
        float frameMs;
        float percent;
    };

    std::deque<Sample> m_samples;
    float m_currentPercent = 0.f;
    static constexpr std::size_t kMaxSamples = 3600;
};

} // namespace zaidultra
