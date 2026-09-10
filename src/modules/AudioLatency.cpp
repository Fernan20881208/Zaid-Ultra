#include "AudioLatency.hpp"

#include "../core/Settings.hpp"
#include "../platform/AndroidBridge.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace zaid::ultra {

AudioLatency& AudioLatency::get() {
    static AudioLatency instance;
    return instance;
}

void AudioLatency::refreshDiagnostics() {
    auto mode = settings::text("audio-buffer-mode");
    auto android = AndroidBridge::get().queryAudioProperties();

    {
        std::lock_guard lock(m_mutex);
        m_status.mode = mode;
        m_status.androidSampleRate = android.sampleRate;
        m_status.androidFramesPerBuffer = android.framesPerBuffer;
        m_status.lowLatencyFeature = android.lowLatencyFeature;
        m_status.output = "FMOD diferido";
        m_status.driver = "sin enlace directo";
        m_status.changeRequested = mode != "Original";
        m_status.changeApplied = false;
        m_status.lastError = mode == "Original"
            ? android.lastError
            : "Cambio de buffer omitido por compatibilidad de arranque Android";
    }

    if (settings::diagnostics()) {
        log::info(
            "Audio safe diagnostic: mode='{}', Android={} Hz/{} frames, low-latency={}",
            mode,
            android.sampleRate,
            android.framesPerBuffer,
            android.lowLatencyFeature
        );
    }
}

AudioStatus AudioLatency::status() const {
    std::lock_guard lock(m_mutex);
    return m_status;
}


} // namespace zaid::ultra
