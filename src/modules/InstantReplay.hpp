#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace zaid::ultra {

struct InstantReplayStatus final {
    bool enabled = false;
    bool starting = false;
    bool buffering = false;
    bool saving = false;
    bool videoSupported = false;
    bool audioIncluded = false;
    double audioBufferedSeconds = 0.0;
    std::size_t audioPacketCount = 0;
    double bufferedSeconds = 0.0;
    double bufferedMiB = 0.0;
    std::size_t frameCount = 0;
    std::string summary = "desactivado";
    std::string audioSummary = "audio desactivado";
    std::string audioError;
    std::string lastFile;
    std::string lastError;
};

// A bounded, encoded replay ring. The system compositor and hardware AVC
// encoder live in Android's ROOT screenrecord process; Geometry Dash only
// receives the already-compressed stream on a background thread.
class InstantReplay final {
public:
    struct EncodedFrame final {
        std::vector<std::uint8_t> data;
        std::int64_t ptsUs = 0;
        bool keyFrame = false;
    };

    static InstantReplay& get();

    void beginGameplay();
    void endGameplay();
    bool saveLast60Seconds();
    InstantReplayStatus status() const;

private:
    InstantReplay() = default;
    ~InstantReplay();
    InstantReplay(InstantReplay const&) = delete;
    InstantReplay& operator=(InstantReplay const&) = delete;

    void captureLoop(std::uint64_t generation);
    void acceptNal(std::vector<std::uint8_t> nal, std::uint64_t generation);
    void finishAccessUnit(std::uint64_t generation);
    void pruneLocked();
    void setFailure(std::string message, bool captureFailure = true);
    void requestRecorderStop() const;

    static bool isFirstSlice(std::vector<std::uint8_t> const& nal);
    static std::string shellQuote(std::string const& value);

    mutable std::mutex m_mutex;
    InstantReplayStatus m_status;
    std::deque<EncodedFrame> m_frames;
    std::vector<std::uint8_t> m_sps;
    std::vector<std::uint8_t> m_pps;
    std::vector<std::uint8_t> m_currentAccessUnit;
    bool m_currentHasVcl = false;
    bool m_currentKeyFrame = false;
    std::size_t m_ringBytes = 0;
    std::int64_t m_firstPtsUs = 0;
    std::int64_t m_lastPtsUs = 0;
    std::atomic_bool m_stopRequested{false};
    std::thread m_captureThread;
    std::thread m_saveThread;
    std::uint64_t m_generation = 0;
};

} // namespace zaid::ultra
