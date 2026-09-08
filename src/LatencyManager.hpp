#pragma once

#include <atomic>

namespace zaid::ultra {

class LatencyManager final {
public:
    static LatencyManager& get();

    // Called when the mod is loaded. This only performs read-only diagnostics.
    void prime();

    // Called from PlayLayer::init on the game's gameplay/render thread.
    void onGameplayThread();

private:
    LatencyManager() = default;

    void applyThreadTuning();
    void applyRenderTuning();
    void queueRootTuning(long gameThreadId);
    void logTouchStatus(const char* stage) const;

    std::atomic_bool m_rootWorkRunning{false};
};

} // namespace zaid::ultra
