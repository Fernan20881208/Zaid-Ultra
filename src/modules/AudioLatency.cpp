#include "AudioLatency.hpp"

#include "../core/Settings.hpp"
#include "../platform/AndroidBridge.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <array>

using namespace geode::prelude;

namespace zaid::ultra {

AudioLatency& AudioLatency::get() {
    static AudioLatency instance;
    return instance;
}

void AudioLatency::beforeSystemInit(FMOD::System* system) {
    if (!system) {
        return;
    }

    auto mode = settings::text("audio-buffer-mode");
    auto android = AndroidBridge::get().queryAudioProperties();
    unsigned int requestedBlock = 0;
    if (mode == "Auto seguro" && android.framesPerBuffer > 0) {
        requestedBlock = safeAutoBlockSize(android.framesPerBuffer);
    } else if (mode == "256 x 4 experimental") {
        requestedBlock = 256;
    }

    FMOD_RESULT changeResult = FMOD_OK;
    if (requestedBlock > 0) {
        changeResult = system->setDSPBufferSize(requestedBlock, 4);
    }

    {
        std::lock_guard lock(m_mutex);
        m_system = system;
        m_status.mode = mode;
        m_status.androidSampleRate = android.sampleRate;
        m_status.androidFramesPerBuffer = android.framesPerBuffer;
        m_status.lowLatencyFeature = android.lowLatencyFeature;
        m_status.changeRequested = requestedBlock > 0;
        m_status.changeApplied = requestedBlock > 0 && changeResult == FMOD_OK;
        if (mode == "Auto seguro" && android.framesPerBuffer <= 0) {
            m_status.lastError = "Auto seguro conservó el buffer original: Android no publicó frames-per-buffer";
        } else if (changeResult != FMOD_OK) {
            m_status.lastError = fmt::format("FMOD setDSPBufferSize falló ({})", static_cast<int>(changeResult));
        }
    }

    if (settings::diagnostics()) {
        log::info(
            "Audio pre-init: mode='{}', Android={} Hz/{} frames, requested={}x4, FMOD={}",
            mode,
            android.sampleRate,
            android.framesPerBuffer,
            requestedBlock,
            static_cast<int>(changeResult)
        );
    }
}

void AudioLatency::afterSystemInit(FMOD::System* system, FMOD_RESULT result) {
    {
        std::lock_guard lock(m_mutex);
        m_system = system;
        m_status.initialized = result == FMOD_OK;
        if (result != FMOD_OK) {
            m_status.lastError = fmt::format("FMOD init falló ({})", static_cast<int>(result));
        }
    }
    if (result == FMOD_OK) {
        readSystemDiagnostics(system);
    }
}

void AudioLatency::refreshDiagnostics() {
    FMOD::System* system = nullptr;
    {
        std::lock_guard lock(m_mutex);
        system = m_system;
    }
    if (system) {
        readSystemDiagnostics(system);
    }
}

AudioStatus AudioLatency::status() const {
    std::lock_guard lock(m_mutex);
    return m_status;
}

unsigned int AudioLatency::safeAutoBlockSize(int framesPerBuffer) {
    auto target = static_cast<unsigned int>(std::clamp(framesPerBuffer, 128, 512));
    unsigned int powerOfTwo = 128;
    while (powerOfTwo < target && powerOfTwo < 512) {
        powerOfTwo <<= 1;
    }
    return powerOfTwo;
}

std::string AudioLatency::outputName(FMOD_OUTPUTTYPE output) {
    switch (output) {
        case FMOD_OUTPUTTYPE_AAUDIO: return "AAudio";
        case FMOD_OUTPUTTYPE_OPENSL: return "OpenSL ES";
        case FMOD_OUTPUTTYPE_AUDIOTRACK: return "AudioTrack";
        case FMOD_OUTPUTTYPE_AUTODETECT: return "Autodetect";
        case FMOD_OUTPUTTYPE_UNKNOWN: return "Unknown";
        default: return fmt::format("FMOD output {}", static_cast<int>(output));
    }
}

void AudioLatency::readSystemDiagnostics(FMOD::System* system) {
    if (!system) {
        return;
    }

    unsigned int blockFrames = 0;
    int bufferCount = 0;
    int sampleRate = 0;
    FMOD_SPEAKERMODE speakerMode = FMOD_SPEAKERMODE_DEFAULT;
    int rawSpeakers = 0;
    FMOD_OUTPUTTYPE output = FMOD_OUTPUTTYPE_UNKNOWN;
    int driverIndex = 0;
    std::array<char, 256> driverName{};
    int driverRate = 0;
    FMOD_SPEAKERMODE driverSpeakerMode = FMOD_SPEAKERMODE_DEFAULT;
    int driverChannels = 0;
    FMOD_CPU_USAGE cpu{};

    auto bufferResult = system->getDSPBufferSize(&blockFrames, &bufferCount);
    auto formatResult = system->getSoftwareFormat(&sampleRate, &speakerMode, &rawSpeakers);
    auto outputResult = system->getOutput(&output);
    auto driverResult = system->getDriver(&driverIndex);
    FMOD_RESULT driverInfoResult = FMOD_ERR_INVALID_PARAM;
    if (driverResult == FMOD_OK) {
        driverInfoResult = system->getDriverInfo(
            driverIndex,
            driverName.data(),
            static_cast<int>(driverName.size()),
            nullptr,
            &driverRate,
            &driverSpeakerMode,
            &driverChannels
        );
    }
    auto cpuResult = system->getCPUUsage(&cpu);

    std::lock_guard lock(m_mutex);
    if (bufferResult == FMOD_OK) {
        m_status.dspBlockFrames = blockFrames;
        m_status.dspBufferCount = bufferCount;
    }
    if (formatResult == FMOD_OK) {
        m_status.softwareSampleRate = sampleRate;
    }
    if (outputResult == FMOD_OK) {
        m_status.output = outputName(output);
    }
    if (driverInfoResult == FMOD_OK) {
        m_status.driver = driverName.data();
        if (m_status.softwareSampleRate <= 0) {
            m_status.softwareSampleRate = driverRate;
        }
    }
    if (cpuResult == FMOD_OK) {
        m_status.dspCpuPercent = cpu.dsp;
        m_status.streamCpuPercent = cpu.stream;
    }
    if (m_status.softwareSampleRate > 0 && m_status.dspBlockFrames > 0 && m_status.dspBufferCount > 0) {
        m_status.nominalMixQueueMs = 1000.0f *
            static_cast<float>(m_status.dspBlockFrames * m_status.dspBufferCount) /
            static_cast<float>(m_status.softwareSampleRate);
    }
    if (settings::diagnostics()) {
        log::info(
            "FMOD audio: output={}, driver='{}', software={} Hz, DSP={}x{} (~{:.2f} ms), CPU dsp={:.2f}% stream={:.2f}%",
            m_status.output,
            m_status.driver,
            m_status.softwareSampleRate,
            m_status.dspBlockFrames,
            m_status.dspBufferCount,
            m_status.nominalMixQueueMs,
            m_status.dspCpuPercent,
            m_status.streamCpuPercent
        );
    }
}

} // namespace zaid::ultra
