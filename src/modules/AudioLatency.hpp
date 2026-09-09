#pragma once

#include <Geode/fmod/fmod.hpp>

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
    std::string underrunDiagnostic = "FMOD Core no expone contador de underruns del mixer";
    std::string lastError;
};

class AudioLatency final {
public:
    static AudioLatency& get();

    // Called by the FMOD::System::init hook. setDSPBufferSize is only valid
    // before init, so this is the sole mutation point for audio buffering.
    void beforeSystemInit(FMOD::System* system);
    void afterSystemInit(FMOD::System* system, FMOD_RESULT result);
    void refreshDiagnostics();
    AudioStatus status() const;

private:
    AudioLatency() = default;
    static unsigned int safeAutoBlockSize(int framesPerBuffer);
    static std::string outputName(FMOD_OUTPUTTYPE output);
    void readSystemDiagnostics(FMOD::System* system);

    mutable std::mutex m_mutex;
    FMOD::System* m_system = nullptr;
    AudioStatus m_status;
};

} // namespace zaid::ultra
