#pragma once

#include <array>
#include <cstddef>

namespace zaidultra {

enum class DetailTier {
    Normal = 0,
    LDM = 1,
    ULDM = 2,
};

struct DetailDecision {
    DetailTier tier = DetailTier::Normal;
    float learnedFrameMs = 0.f;
    float budgetMs = 0.f;
    float confidence = 0.f;
};

class SmartDetail final {
public:
    static SmartDetail& get();

    void resetSession();
    void observe(float percent, float frameMs, float targetFps);

    [[nodiscard]] DetailDecision decision(float percent, float targetFps) const;
    [[nodiscard]] DetailTier activeTier() const;

private:
    struct Bucket {
        float emaFrameMs = 0.f;
        std::size_t samples = 0;
    };

    std::array<Bucket, 101> m_buckets{};
    DetailTier m_activeTier = DetailTier::Normal;
    int m_upgradeEvidence = 0;
    int m_recoveryEvidence = 0;

    [[nodiscard]] static std::size_t bucketFor(float percent);
};

} // namespace zaidultra
