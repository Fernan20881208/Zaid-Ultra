#pragma once

#include <array>
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

struct SectionSnapshot {
    bool valid = false;
    float startPercent = 0.f;
    float endPercent = 0.f;
    float averageFrameMs = 0.f;
    float worstFrameMs = 0.f;
    float loadRatio = 0.f;
    std::size_t samples = 0;
};

class Telemetry final {
public:
    static Telemetry& get();

    void resetLevel();
    void resetAttempt();
    void sample(float dt, float percent);

    [[nodiscard]] FrameSnapshot snapshot(std::size_t window = 240) const;
    [[nodiscard]] float performanceScore(float targetFps) const;
    [[nodiscard]] float levelPerformanceScore(float targetFps) const;
    [[nodiscard]] SectionSnapshot worstSection(float targetFps, int sectionWidth = 5) const;
    [[nodiscard]] float currentPercent() const;

private:
    struct Sample {
        float frameMs;
        float percent;
    };

    struct LevelBucket {
        double totalFrameMs = 0.0;
        float worstFrameMs = 0.f;
        std::size_t samples = 0;
    };

    std::deque<Sample> m_samples;
    std::array<LevelBucket, 101> m_levelBuckets{};
    float m_currentPercent = 0.f;
    static constexpr std::size_t kMaxSamples = 3600;
};

} // namespace zaidultra
