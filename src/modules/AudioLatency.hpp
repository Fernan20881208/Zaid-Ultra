#pragma once

#include <mutex>
#include <string>

namespace zaid::ultra {

struct AudioStatus final {
    bool initialized = false;
    bool changeRequested = false;
    bool changeApplied = false;
    unsigned int dspBlockFrames = 0;
    int dspBufferCount = 0;
    int softwareSampleRate = 0;
    int androidSampleRate = 0;
    int androidFramesPerBuffer = 0;
    float nominalMixQueueMs = 0.0f;
    float dspCpuPercent = 0.0f;
    float streamCpuPercent = 0.0f;
    std::string output = "desconocido";
    std::string driver = "<no disponible>";
    std::string lowLatencyFeature = "desconocido";
    std::string mode = "Original";
    std::string underrunDiagnostic = "FMOD directo desactivado en el arranque seguro";
    std::string lastError;
};

class AudioLatency final {
public:
    static AudioLatency& get();

    // Android properties remain available without linking to or hooking FMOD.
    // Direct FMOD diagnostics/tuning stay disabled until the device crash path
    // is isolated and a late, ABI-safe integration is validated.
    void refreshDiagnostics();
    AudioStatus status() const;

private:
    AudioLatency() = default;

    mutable std::mutex m_mutex;
    AudioStatus m_status;
};

} // namespace zaid::ultra
