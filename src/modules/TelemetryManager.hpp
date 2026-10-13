#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace zaid::ultra {

struct TelemetrySnapshot final {
    double fps = 0.0;
    double frameTimeMs = 0.0;
    double frameTimeP95Ms = 0.0;
    double refreshRateHz = 0.0;
    double touchDeliveryHz = 0.0;
    double touchJitterMs = 0.0;
    double inputDispatchAgeMs = 0.0;
    double inputToPhysicsMs = 0.0;
    double cbfStepPhaseMs = 0.0;
    double cbfOffsetToNextStepMs = 0.0;
    double cbfOffsetJitterMs = 0.0;
    double cpuTemperatureC = 0.0;
    double gpuTemperatureC = 0.0;
    double batteryTemperatureC = 0.0;
    double cpuCurrentMHz = 0.0;
    double cpuMaximumMHz = 0.0;
    double gpuCurrentMHz = 0.0;
    double gpuMaximumMHz = 0.0;
    std::int64_t thermalPressure = 0;
    bool throttling = false;
    bool throttlingHeuristic = false;
    std::string throttlingReason = "sin evidencia";
    std::string sensorNote = "sin muestreo";
};

class TelemetryManager final {
public:
    static TelemetryManager& get();

    void start(bool sampleThermals);
    void stop();
    void requestSample();

    void onFrame();
    void onTouch(std::int64_t eventTimestampNs, int eventType);
    void onPhysicsBoundary();

    TelemetrySnapshot snapshot() const;
    std::string compactText() const;

private:
    struct ThermalSource final {
        std::string type;
        std::string temperaturePath;
    };

    TelemetryManager() = default;
    ~TelemetryManager();
    TelemetryManager(TelemetryManager const&) = delete;
    TelemetryManager& operator=(TelemetryManager const&) = delete;

    void sensorLoop();
    void discoverSources();
    void sampleSensors();
    void recomputeFrameMetricsLocked();

    static std::int64_t monotonicNs();

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_sensorThread;
    bool m_active = false;
    bool m_stopRequested = false;
    bool m_sampleRequested = false;
    bool m_sampleThermals = false;

    TelemetrySnapshot m_snapshot;
    std::array<double, 240> m_frameIntervalsMs{};
    std::size_t m_frameCount = 0;
    std::size_t m_frameIndex = 0;
    std::int64_t m_lastFrameNs = 0;
    std::int64_t m_lastFrameAggregateNs = 0;
    std::int64_t m_lastDisplaySampleNs = 0;
    double m_touchMeanMs = 0.0;
    double m_touchVarianceMs2 = 0.0;
    double m_cbfOffsetMeanMs = 0.0;
    double m_cbfOffsetVarianceMs2 = 0.0;
    std::int64_t m_lastTouchMoveNs = 0;
    std::atomic<std::int64_t> m_pendingInputNs{0};
    std::atomic<std::int64_t> m_pendingPhaseInputNs{0};

    std::vector<ThermalSource> m_thermalSources;
    std::vector<std::string> m_cpuPolicyPaths;
    std::string m_gpuDevfreqPath;
};

} // namespace zaid::ultra
