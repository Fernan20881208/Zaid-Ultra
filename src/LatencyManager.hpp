#pragma once

#include <mutex>
#include <string>

namespace zaid::ultra {

struct ThreadTuningStatus final {
    bool active = false;
    long threadId = -1;
    long originalTimerSlackNs = -1;
    int originalNice = 0;
    bool timerSlackCaptured = false;
    bool niceCaptured = false;
    bool timerSlackApplied = false;
    bool priorityApplied = false;
    bool priorityRootFallback = false;
    bool noVsyncApplied = false;
    std::string lastAction = "sin ejecutar";
};

// Owns only process-local/thread-local tuning. Persistent Android settings
// and sysfs nodes are exclusively owned by RootStateGuard.
class LatencyManager final {
public:
    static LatencyManager& get();

    void prime();
    void begin();
    void end();
    ThreadTuningStatus status() const;

private:
    LatencyManager() = default;

    static long currentThreadId();
    void queuePriorityChange(long threadId, int nice, bool restoring);

    mutable std::mutex m_mutex;
    ThreadTuningStatus m_status;
};

} // namespace zaid::ultra
