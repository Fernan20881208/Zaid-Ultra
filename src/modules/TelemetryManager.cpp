#include "TelemetryManager.hpp"

#include "../core/Settings.hpp"
#include "../platform/AndroidBridge.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <time.h>

using namespace geode::prelude;

namespace zaid::ultra {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string readText(std::filesystem::path const& path) {
    std::ifstream file(path);
    std::string value;
    if (!file.good() || !std::getline(file, value)) {
        return {};
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::int64_t readInteger(std::filesystem::path const& path) {
    auto text = readText(path);
    if (text.empty()) {
        return 0;
    }
    char* end = nullptr;
    auto value = std::strtoll(text.c_str(), &end, 10);
    return end != text.c_str() && *end == '\0' ? value : 0;
}

double normalizeThermal(std::int64_t raw, bool battery) {
    auto absolute = std::llabs(raw);
    if (absolute >= 1000) {
        return static_cast<double>(raw) / 1000.0;
    }
    if (battery && absolute >= 100) {
        return static_cast<double>(raw) / 10.0;
    }
    return static_cast<double>(raw);
}

double normalizeFrequencyMHz(std::int64_t raw) {
    auto absolute = std::llabs(raw);
    if (absolute >= 10'000'000) {
        return static_cast<double>(raw) / 1'000'000.0;
    }
    if (absolute >= 10'000) {
        return static_cast<double>(raw) / 1000.0;
    }
    return static_cast<double>(raw);
}

bool containsAny(std::string const& value, std::initializer_list<char const*> needles) {
    for (auto needle : needles) {
        if (value.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

TelemetryManager& TelemetryManager::get() {
    static TelemetryManager instance;
    return instance;
}

TelemetryManager::~TelemetryManager() {
    stop();
}

void TelemetryManager::start(bool sampleThermals) {
    stop();
    {
        std::lock_guard lock(m_mutex);
        m_active = true;
        m_stopRequested = false;
        m_sampleRequested = true;
        m_sampleThermals = sampleThermals;
        m_snapshot = {};
        m_frameCount = 0;
        m_frameIndex = 0;
        m_lastFrameNs = 0;
        m_lastFrameAggregateNs = 0;
        m_lastDisplaySampleNs = 0;
        m_touchMeanMs = 0.0;
        m_touchVarianceMs2 = 0.0;
        m_cbfOffsetMeanMs = 0.0;
        m_cbfOffsetVarianceMs2 = 0.0;
        m_lastTouchMoveNs = 0;
        m_pendingInputNs.store(0);
        m_pendingPhaseInputNs.store(0);
    }
    m_sensorThread = std::thread([this] { sensorLoop(); });
}

void TelemetryManager::stop() {
    {
        std::lock_guard lock(m_mutex);
        m_active = false;
        m_stopRequested = true;
    }
    m_cv.notify_all();
    if (m_sensorThread.joinable()) {
        m_sensorThread.join();
    }
}

void TelemetryManager::requestSample() {
    {
        std::lock_guard lock(m_mutex);
        m_sampleRequested = true;
    }
    m_cv.notify_one();
}

void TelemetryManager::onFrame() {
    auto now = monotonicNs();
    bool sampleDisplay = false;
    auto phaseInput = m_pendingPhaseInputNs.exchange(0, std::memory_order_acq_rel);
    {
        std::lock_guard lock(m_mutex);
        if (!m_active) {
            return;
        }
        if (m_lastFrameNs > 0) {
            auto interval = static_cast<double>(now - m_lastFrameNs) / 1'000'000.0;
            if (interval > 0.1 && interval < 1000.0) {
                m_frameIntervalsMs[m_frameIndex] = interval;
                m_frameIndex = (m_frameIndex + 1) % m_frameIntervalsMs.size();
                m_frameCount = std::min(m_frameCount + 1, m_frameIntervalsMs.size());
            }

            // Mirror the timestamp math CBF uses without touching its queue:
            // divide the observed frame span into ~240 Hz physics intervals
            // and locate the input inside its interval. Hook ordering can add
            // a small constant offset, so this is explicitly an estimate.
            if (phaseInput > m_lastFrameNs && phaseInput <= now) {
                auto frameSpanNs = static_cast<double>(now - m_lastFrameNs);
                auto steps = std::max(1LL, std::llround(frameSpanNs / 4'166'666.667));
                auto stepNs = frameSpanNs / static_cast<double>(steps);
                auto elapsedNs = static_cast<double>(phaseInput - m_lastFrameNs);
                auto phaseNs = std::fmod(elapsedNs, stepNs);
                auto offsetMs = (stepNs - phaseNs) / 1'000'000.0;
                m_snapshot.cbfStepPhaseMs = phaseNs / 1'000'000.0;
                m_snapshot.cbfOffsetToNextStepMs = offsetMs;
                constexpr double alpha = 0.15;
                if (m_cbfOffsetMeanMs == 0.0) {
                    m_cbfOffsetMeanMs = offsetMs;
                } else {
                    auto difference = offsetMs - m_cbfOffsetMeanMs;
                    m_cbfOffsetMeanMs += alpha * difference;
                    m_cbfOffsetVarianceMs2 = (1.0 - alpha) *
                        (m_cbfOffsetVarianceMs2 + alpha * difference * difference);
                }
                m_snapshot.cbfOffsetJitterMs = std::sqrt(std::max(0.0, m_cbfOffsetVarianceMs2));
            }
        }
        m_lastFrameNs = now;
        if (m_lastFrameAggregateNs == 0 || now - m_lastFrameAggregateNs >= 250'000'000LL) {
            recomputeFrameMetricsLocked();
            m_lastFrameAggregateNs = now;
        }
        if (m_lastDisplaySampleNs == 0 || now - m_lastDisplaySampleNs >= 500'000'000LL) {
            m_lastDisplaySampleNs = now;
            sampleDisplay = true;
        }
    }

    if (sampleDisplay) {
        auto refresh = AndroidBridge::get().sampleRefreshRate();
        if (refresh > 0.0f) {
            double previous = 0.0;
            {
                std::lock_guard lock(m_mutex);
                previous = m_snapshot.refreshRateHz;
                m_snapshot.refreshRateHz = refresh;
            }
            if (settings::diagnostics() && previous > 0.0 && std::fabs(previous - refresh) >= 5.0) {
                if (refresh < 105.0) {
                    log::warn("HyperOS/display refresh transition: {:.1f} -> {:.1f} Hz", previous, refresh);
                } else {
                    log::info("Display refresh transition: {:.1f} -> {:.1f} Hz", previous, refresh);
                }
            }
        }
    }
}

void TelemetryManager::onTouch(std::int64_t eventTimestampNs, int eventType) {
    if (!settings::enabled("cbf-hardware-metrics")) {
        return;
    }
    auto now = monotonicNs();
    if (eventTimestampNs <= 0 || now <= eventTimestampNs) {
        return;
    }
    auto dispatchAge = static_cast<double>(now - eventTimestampNs) / 1'000'000.0;

    std::lock_guard lock(m_mutex);
    if (!m_active) {
        return;
    }
    m_snapshot.inputDispatchAgeMs = dispatchAge;

    // AndroidTouchInput::Type::Moved == 1. This measures events delivered to
    // Geode, which may be lower than the Goodix SYN_REPORT hardware rate when
    // Android batches/coalesces MotionEvents.
    if (eventType == 1) {
        if (m_lastTouchMoveNs > 0) {
            auto interval = static_cast<double>(eventTimestampNs - m_lastTouchMoveNs) / 1'000'000.0;
            if (interval >= 0.2 && interval <= 50.0) {
                constexpr double alpha = 0.08;
                if (m_touchMeanMs == 0.0) {
                    m_touchMeanMs = interval;
                } else {
                    auto difference = interval - m_touchMeanMs;
                    m_touchMeanMs += alpha * difference;
                    m_touchVarianceMs2 = (1.0 - alpha) *
                        (m_touchVarianceMs2 + alpha * difference * difference);
                }
                m_snapshot.touchDeliveryHz = m_touchMeanMs > 0.0 ? 1000.0 / m_touchMeanMs : 0.0;
                m_snapshot.touchJitterMs = std::sqrt(std::max(0.0, m_touchVarianceMs2));
            }
        }
        m_lastTouchMoveNs = eventTimestampNs;
    }

    // Began (0) and Ended (2) are the action timestamps CBF/CBS consume.
    if (eventType == 0 || eventType == 2) {
        m_pendingInputNs.store(eventTimestampNs, std::memory_order_release);
        m_pendingPhaseInputNs.store(eventTimestampNs, std::memory_order_release);
    }
}

void TelemetryManager::onPhysicsBoundary() {
    auto timestamp = m_pendingInputNs.exchange(0, std::memory_order_acq_rel);
    if (timestamp <= 0) {
        return;
    }
    auto now = monotonicNs();
    if (now <= timestamp) {
        return;
    }
    auto latency = static_cast<double>(now - timestamp) / 1'000'000.0;
    if (latency > 500.0) {
        return;
    }
    std::lock_guard lock(m_mutex);
    if (!m_active) {
        return;
    }
    m_snapshot.inputToPhysicsMs = m_snapshot.inputToPhysicsMs == 0.0
        ? latency
        : (m_snapshot.inputToPhysicsMs * 0.8 + latency * 0.2);
}

TelemetrySnapshot TelemetryManager::snapshot() const {
    std::lock_guard lock(m_mutex);
    return m_snapshot;
}

std::string TelemetryManager::compactText() const {
    auto value = snapshot();
    auto throttle = value.throttling
        ? (value.throttlingHeuristic ? "PROBABLE" : "SI")
        : "no";
    return fmt::format(
        "FPS {:.0f} | {:.2f} ms (P95 {:.2f})\n"
        "Pantalla {:.0f} Hz | Touch app {:.0f} Hz, jitter {:.2f} ms\n"
        "Input {:.2f} ms -> fisica {:.2f} ms\n"
        "CBF fase {:.2f} ms | prox. step {:.2f} ms (jitter {:.2f})\n"
        "CPU {:.0f}/{:.0f} MHz {:.1f}C | GPU {:.0f}/{:.0f} MHz {:.1f}C\n"
        "Bateria {:.1f}C | Throttle {}",
        value.fps,
        value.frameTimeMs,
        value.frameTimeP95Ms,
        value.refreshRateHz,
        value.touchDeliveryHz,
        value.touchJitterMs,
        value.inputDispatchAgeMs,
        value.inputToPhysicsMs,
        value.cbfStepPhaseMs,
        value.cbfOffsetToNextStepMs,
        value.cbfOffsetJitterMs,
        value.cpuCurrentMHz,
        value.cpuMaximumMHz,
        value.cpuTemperatureC,
        value.gpuCurrentMHz,
        value.gpuMaximumMHz,
        value.gpuTemperatureC,
        value.batteryTemperatureC,
        throttle
    );
}

void TelemetryManager::sensorLoop() {
    discoverSources();
    for (;;) {
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait_for(lock, std::chrono::seconds(1), [this] {
                return m_stopRequested || m_sampleRequested;
            });
            if (m_stopRequested) {
                return;
            }
            m_sampleRequested = false;
        }
        sampleSensors();
    }
}

void TelemetryManager::discoverSources() {
#ifdef GEODE_IS_ANDROID
    std::vector<ThermalSource> thermalSources;
    std::vector<std::string> cpuPolicies;
    std::string gpuPath;
    std::error_code error;

    auto thermalRoot = std::filesystem::path("/sys/class/thermal");
    if (std::filesystem::exists(thermalRoot, error)) {
        for (auto const& entry : std::filesystem::directory_iterator(thermalRoot, error)) {
            auto name = entry.path().filename().string();
            if (!name.starts_with("thermal_zone")) {
                continue;
            }
            auto type = lower(readText(entry.path() / "type"));
            auto temp = entry.path() / "temp";
            if (!type.empty() && std::filesystem::exists(temp, error)) {
                thermalSources.push_back({type, temp.string()});
            }
        }
    }

    auto cpuRoot = std::filesystem::path("/sys/devices/system/cpu/cpufreq");
    if (std::filesystem::exists(cpuRoot, error)) {
        for (auto const& entry : std::filesystem::directory_iterator(cpuRoot, error)) {
            if (entry.path().filename().string().starts_with("policy")) {
                cpuPolicies.push_back(entry.path().string());
            }
        }
    }

    auto devfreqRoot = std::filesystem::path("/sys/class/devfreq");
    if (std::filesystem::exists(devfreqRoot, error)) {
        for (auto const& entry : std::filesystem::directory_iterator(devfreqRoot, error)) {
            auto identity = lower(entry.path().filename().string() + " " + readText(entry.path() / "name"));
            if (containsAny(identity, {"gpu", "mali", "kgsl"})) {
                gpuPath = entry.path().string();
                break;
            }
        }
    }

    std::lock_guard lock(m_mutex);
    m_thermalSources = std::move(thermalSources);
    m_cpuPolicyPaths = std::move(cpuPolicies);
    m_gpuDevfreqPath = std::move(gpuPath);
#endif
}

void TelemetryManager::sampleSensors() {
#ifdef GEODE_IS_ANDROID
    std::vector<ThermalSource> thermals;
    std::vector<std::string> policies;
    std::string gpuPath;
    bool sampleThermals = false;
    {
        std::lock_guard lock(m_mutex);
        thermals = m_thermalSources;
        policies = m_cpuPolicyPaths;
        gpuPath = m_gpuDevfreqPath;
        sampleThermals = m_sampleThermals;
    }

    auto update = snapshot();
    // Sensor readings are a current sample, not a session maximum. Reset
    // fields that may legitimately fall between samples.
    update.cpuTemperatureC = 0.0;
    update.gpuTemperatureC = 0.0;
    update.batteryTemperatureC = 0.0;
    update.cpuCurrentMHz = 0.0;
    update.cpuMaximumMHz = 0.0;
    update.gpuCurrentMHz = 0.0;
    update.gpuMaximumMHz = 0.0;
    update.thermalPressure = 0;
    update.throttling = false;
    update.throttlingHeuristic = false;

    if (sampleThermals) {
        for (auto const& source : thermals) {
            auto raw = readInteger(source.temperaturePath);
            if (raw == 0) {
                continue;
            }
            auto value = normalizeThermal(raw, false);
            if (containsAny(source.type, {"gpu", "mali"})) {
                update.gpuTemperatureC = std::max(update.gpuTemperatureC, value);
            }
            if (containsAny(source.type, {"cpu", "soc", "ap", "little", "big"})) {
                update.cpuTemperatureC = std::max(update.cpuTemperatureC, value);
            }
        }

        auto batteryRaw = readInteger("/sys/class/power_supply/battery/temp");
        if (batteryRaw != 0) {
            update.batteryTemperatureC = normalizeThermal(batteryRaw, true);
        }
    }

    double currentCpu = 0.0;
    double maximumCpu = 0.0;
    for (auto const& policy : policies) {
        currentCpu = std::max(currentCpu, normalizeFrequencyMHz(readInteger(std::filesystem::path(policy) / "scaling_cur_freq")));
        maximumCpu = std::max(maximumCpu, normalizeFrequencyMHz(readInteger(std::filesystem::path(policy) / "cpuinfo_max_freq")));
    }
    update.cpuCurrentMHz = currentCpu;
    update.cpuMaximumMHz = maximumCpu;

    if (!gpuPath.empty()) {
        update.gpuCurrentMHz = normalizeFrequencyMHz(readInteger(std::filesystem::path(gpuPath) / "cur_freq"));
        update.gpuMaximumMHz = normalizeFrequencyMHz(readInteger(std::filesystem::path(gpuPath) / "max_freq"));
    }

    std::int64_t maximumPressure = 0;
    std::error_code error;
    auto cpuRoot = std::filesystem::path("/sys/devices/system/cpu");
    if (std::filesystem::exists(cpuRoot, error)) {
        for (auto const& entry : std::filesystem::directory_iterator(cpuRoot, error)) {
            auto name = entry.path().filename().string();
            if (!name.starts_with("cpu") || name.size() <= 3 || !std::isdigit(static_cast<unsigned char>(name[3]))) {
                continue;
            }
            maximumPressure = std::max(maximumPressure, readInteger(entry.path() / "thermal_pressure"));
        }
    }
    update.thermalPressure = maximumPressure;
    update.throttling = maximumPressure > 0;
    update.throttlingHeuristic = false;
    update.throttlingReason = update.throttling
        ? fmt::format("thermal_pressure={}", maximumPressure)
        : "sin evidencia";

    if (!update.throttling && update.cpuTemperatureC >= 80.0 &&
        update.cpuCurrentMHz > 0.0 && update.cpuMaximumMHz > 0.0 &&
        update.cpuCurrentMHz < update.cpuMaximumMHz * 0.65) {
        update.throttling = true;
        update.throttlingHeuristic = true;
        update.throttlingReason = "temperatura alta + frecuencia CPU reducida (heurística)";
    }

    update.sensorNote = fmt::format(
        "thermal zones={} cpu policies={} gpu devfreq={}",
        thermals.size(),
        policies.size(),
        gpuPath.empty() ? "no" : "sí"
    );

    {
        std::lock_guard lock(m_mutex);
        // Preserve frame/input/display values that may have changed while the
        // read-only sysfs sampling ran.
        update.fps = m_snapshot.fps;
        update.frameTimeMs = m_snapshot.frameTimeMs;
        update.frameTimeP95Ms = m_snapshot.frameTimeP95Ms;
        update.refreshRateHz = m_snapshot.refreshRateHz;
        update.touchDeliveryHz = m_snapshot.touchDeliveryHz;
        update.touchJitterMs = m_snapshot.touchJitterMs;
        update.inputDispatchAgeMs = m_snapshot.inputDispatchAgeMs;
        update.inputToPhysicsMs = m_snapshot.inputToPhysicsMs;
        update.cbfStepPhaseMs = m_snapshot.cbfStepPhaseMs;
        update.cbfOffsetToNextStepMs = m_snapshot.cbfOffsetToNextStepMs;
        update.cbfOffsetJitterMs = m_snapshot.cbfOffsetJitterMs;
        m_snapshot = std::move(update);
    }
#endif
}

void TelemetryManager::recomputeFrameMetricsLocked() {
    if (m_frameCount == 0) {
        return;
    }
    std::vector<double> intervals;
    intervals.reserve(m_frameCount);
    for (std::size_t index = 0; index < m_frameCount; ++index) {
        intervals.push_back(m_frameIntervalsMs[index]);
    }
    auto total = std::accumulate(intervals.begin(), intervals.end(), 0.0);
    auto average = total / static_cast<double>(intervals.size());
    std::sort(intervals.begin(), intervals.end());
    auto p95Index = static_cast<std::size_t>(std::ceil(intervals.size() * 0.95)) - 1;
    p95Index = std::min(p95Index, intervals.size() - 1);
    m_snapshot.frameTimeMs = average;
    m_snapshot.frameTimeP95Ms = intervals[p95Index];
    m_snapshot.fps = average > 0.0 ? 1000.0 / average : 0.0;
}

std::int64_t TelemetryManager::monotonicNs() {
    timespec value{};
    ::clock_gettime(CLOCK_MONOTONIC, &value);
    return static_cast<std::int64_t>(value.tv_sec) * 1'000'000'000LL + value.tv_nsec;
}

} // namespace zaid::ultra
