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

struct EncodedAudioFrame final {
    std::vector<std::uint8_t> data;
    std::int64_t ptsUs = 0;
    std::uint32_t flags = 0;
};

struct ReplayAudioSnapshot final {
    std::vector<EncodedAudioFrame> frames;
    std::vector<std::uint8_t> codecSpecificData;
};

struct ReplayAudioStatus final {
    bool enabled = false;
    bool starting = false;
    bool buffering = false;
    bool available = false;
    double bufferedSeconds = 0.0;
    double bufferedMiB = 0.0;
    std::size_t packetCount = 0;
    bool signalMeasured = false;
    bool signalPresent = false;
    double signalDbfs = -120.0;
    double signalPeak = 0.0;
    int targetUid = -1;
    std::string summary = "audio desactivado";
    std::string lastError;
};

// Owns the short-lived ROOT app_process audio helper and a bounded AAC ring.
// It changes no persistent Android setting. Process death automatically tears
// down the AudioPolicy loopback route.
class ReplayAudio final {
public:
    static ReplayAudio& get();

    void beginGameplay();
    void endGameplay();
    ReplayAudioSnapshot snapshot(std::int64_t fromPtsUs, std::int64_t toPtsUs) const;
    ReplayAudioStatus status() const;

private:
    ReplayAudio() = default;
    ~ReplayAudio();
    ReplayAudio(ReplayAudio const&) = delete;
    ReplayAudio& operator=(ReplayAudio const&) = delete;

    void captureLoop(std::uint64_t generation, std::string helperSource);
    void acceptPacket(
        std::vector<std::uint8_t> payload,
        std::int64_t ptsUs,
        std::uint32_t flags,
        std::uint64_t generation
    );
    void acceptTelemetry(
        std::vector<std::uint8_t> const& payload,
        std::uint64_t generation
    );
    void pruneLocked();
    void setFailure(std::string message);
    void requestHelperStop() const;

    static std::string shellQuote(std::string const& value);

    mutable std::mutex m_mutex;
    ReplayAudioStatus m_status;
    std::deque<EncodedAudioFrame> m_frames;
    std::vector<std::uint8_t> m_codecSpecificData;
    std::size_t m_ringBytes = 0;
    std::atomic_bool m_stopRequested{false};
    std::thread m_captureThread;
    std::uint64_t m_generation = 0;
};

} // namespace zaid::ultra
