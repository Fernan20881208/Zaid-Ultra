#pragma once

#include <mutex>
#include <string>

namespace zaid::ultra {

struct DisplayStatus final {
    float currentRefreshHz = 0.0f;
    float requestedRefreshHz = 0.0f;
    int selectedModeId = 0;
    bool requestActive = false;
    std::string supportedModes = "<no disponible>";
    std::string lastError;
};

struct AndroidAudioProperties final {
    int sampleRate = 0;
    int framesPerBuffer = 0;
    std::string lowLatencyFeature = "desconocido";
    std::string lastError;
};

class AndroidBridge final {
public:
    static AndroidBridge& get();

    bool requestRefreshRate(float hz);
    void restoreRefreshRate();
    float sampleRefreshRate();
    DisplayStatus displayStatus() const;

    AndroidAudioProperties queryAudioProperties();
    AndroidAudioProperties audioProperties() const;

private:
    struct WindowSnapshot final {
        bool valid = false;
        int preferredModeId = 0;
        float preferredRefreshRate = 0.0f;
    };

    AndroidBridge() = default;

    mutable std::mutex m_mutex;
    WindowSnapshot m_windowSnapshot;
    DisplayStatus m_displayStatus;
    AndroidAudioProperties m_audioProperties;
};

} // namespace zaid::ultra
