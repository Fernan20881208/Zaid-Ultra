#pragma once

#include <cstdint>
#include <mutex>
#include <string>

namespace zaid::ultra {

struct MemoryTrimStatus final {
    bool supported = false;
    bool tested = false;
    bool beneficial = false;
    bool autoDisabled = false;
    std::int64_t rssBeforeKiB = 0;
    std::int64_t rssAfterKiB = 0;
    std::int64_t reclaimedKiB = 0;
    double durationMs = 0.0;
    std::string note = "sin ejecutar";
};

class MemoryTrim final {
public:
    static MemoryTrim& get();

    void beforeGameplay(bool requested);
    void runNow();
    MemoryTrimStatus status() const;

private:
    MemoryTrim() = default;
    void execute(bool manual);
    static std::int64_t readRssKiB();

    mutable std::mutex m_mutex;
    MemoryTrimStatus m_status;
};

} // namespace zaid::ultra
