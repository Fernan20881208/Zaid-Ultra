#include "Telemetry.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace zaidultra {

Telemetry& Telemetry::get() {
    static Telemetry instance;
    return instance;
}

void Telemetry::resetLevel() {
    m_levelBuckets = {};
    resetAttempt();
}

void Telemetry::resetAttempt() {
    m_samples.clear();
    m_currentPercent = 0.f;
}

void Telemetry::sample(float dt, float percent) {
    if (!std::isfinite(dt) || dt <= 0.f || dt > 1.f) {
        return;
    }

    m_currentPercent = std::clamp(percent, 0.f, 100.f);
    const float frameMs = dt * 1000.f;

    m_samples.push_back({frameMs, m_currentPercent});
    while (m_samples.size() > kMaxSamples) {
        m_samples.pop_front();
    }

    const auto bucketIndex = static_cast<std::size_t>(std::clamp(std::lround(m_currentPercent), 0L, 100L));
    auto& bucket = m_levelBuckets[bucketIndex];
    bucket.totalFrameMs += frameMs;
    bucket.worstFrameMs = std::max(bucket.worstFrameMs, frameMs);
    ++bucket.samples;
}

FrameSnapshot Telemetry::snapshot(std::size_t window) const {
    FrameSnapshot out;
    if (m_samples.empty()) {
        return out;
    }

    const auto count = std::min(window, m_samples.size());
    const auto first = m_samples.size() - count;

    std::vector<float> frameTimes;
    frameTimes.reserve(count);
    for (std::size_t i = first; i < m_samples.size(); ++i) {
        frameTimes.push_back(m_samples[i].frameMs);
    }

    const float sum = std::accumulate(frameTimes.begin(), frameTimes.end(), 0.f);
    out.averageFrameMs = sum / static_cast<float>(frameTimes.size());
    out.worstFrameMs = *std::max_element(frameTimes.begin(), frameTimes.end());

    auto sorted = frameTimes;
    std::sort(sorted.begin(), sorted.end(), std::greater<>());
    const auto lowCount = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(sorted.size() * 0.01f)));
    const float lowMs = std::accumulate(sorted.begin(), sorted.begin() + lowCount, 0.f) / static_cast<float>(lowCount);
    out.onePercentLowFps = lowMs > 0.f ? 1000.f / lowMs : 0.f;

    const float mean = out.averageFrameMs;
    float variance = 0.f;
    for (float ms : frameTimes) {
        const float diff = ms - mean;
        variance += diff * diff;
    }
    variance /= static_cast<float>(frameTimes.size());
    const float stddev = std::sqrt(variance);
    const float coefficient = mean > 0.f ? stddev / mean : 0.f;
    out.framePacingScore = std::clamp(100.f - coefficient * 180.f, 0.f, 100.f);

    const float spikeThreshold = std::max(mean * 1.5f, mean + 2.f);
    out.spikeCount = static_cast<int>(std::count_if(frameTimes.begin(), frameTimes.end(), [spikeThreshold](float ms) {
        return ms > spikeThreshold;
    }));

    return out;
}

float Telemetry::performanceScore(float targetFps) const {
    const auto data = snapshot(600);
    if (data.averageFrameMs <= 0.f || targetFps <= 0.f) {
        return 0.f;
    }

    const float budgetMs = 1000.f / targetFps;
    const float budgetScore = std::clamp((budgetMs / data.averageFrameMs) * 100.f, 0.f, 100.f);
    const float lowScore = std::clamp((data.onePercentLowFps / targetFps) * 100.f, 0.f, 100.f);
    const float spikePenalty = std::clamp(static_cast<float>(data.spikeCount) * 0.6f, 0.f, 25.f);

    return std::clamp(budgetScore * 0.45f + lowScore * 0.35f + data.framePacingScore * 0.20f - spikePenalty, 0.f, 100.f);
}

float Telemetry::levelPerformanceScore(float targetFps) const {
    if (targetFps <= 0.f) {
        return 0.f;
    }

    const float budgetMs = 1000.f / targetFps;
    double weightedScore = 0.0;
    std::size_t totalSamples = 0;

    for (auto const& bucket : m_levelBuckets) {
        if (bucket.samples == 0) {
            continue;
        }

        const float avg = static_cast<float>(bucket.totalFrameMs / static_cast<double>(bucket.samples));
        const float avgScore = std::clamp((budgetMs / avg) * 100.f, 0.f, 100.f);
        const float worstScore = bucket.worstFrameMs > 0.f
            ? std::clamp((budgetMs / bucket.worstFrameMs) * 100.f, 0.f, 100.f)
            : 100.f;
        const float sectionScore = avgScore * 0.8f + worstScore * 0.2f;

        weightedScore += static_cast<double>(sectionScore) * static_cast<double>(bucket.samples);
        totalSamples += bucket.samples;
    }

    if (totalSamples == 0) {
        return 0.f;
    }

    return std::clamp(static_cast<float>(weightedScore / static_cast<double>(totalSamples)), 0.f, 100.f);
}

SectionSnapshot Telemetry::worstSection(float targetFps, int sectionWidth) const {
    SectionSnapshot worst;
    if (targetFps <= 0.f) {
        return worst;
    }

    sectionWidth = std::clamp(sectionWidth, 1, 25);
    const float budgetMs = 1000.f / targetFps;
    float worstPressure = -1.f;

    for (int start = 0; start <= 100; start += sectionWidth) {
        const int end = std::min(100, start + sectionWidth - 1);
        double totalMs = 0.0;
        float maxMs = 0.f;
        std::size_t samples = 0;

        for (int i = start; i <= end; ++i) {
            auto const& bucket = m_levelBuckets[static_cast<std::size_t>(i)];
            totalMs += bucket.totalFrameMs;
            maxMs = std::max(maxMs, bucket.worstFrameMs);
            samples += bucket.samples;
        }

        if (samples < 10) {
            continue;
        }

        const float avgMs = static_cast<float>(totalMs / static_cast<double>(samples));
        const float pressure = (avgMs / budgetMs) * 0.75f + (maxMs / budgetMs) * 0.25f;
        if (pressure <= worstPressure) {
            continue;
        }

        worstPressure = pressure;
        worst.valid = true;
        worst.startPercent = static_cast<float>(start);
        worst.endPercent = static_cast<float>(std::min(100, start + sectionWidth));
        worst.averageFrameMs = avgMs;
        worst.worstFrameMs = maxMs;
        worst.loadRatio = pressure;
        worst.samples = samples;
    }

    return worst;
}

float Telemetry::currentPercent() const {
    return m_currentPercent;
}

} // namespace zaidultra
