#include "AndroidGuardian.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>

#ifdef GEODE_IS_ANDROID
#include <filesystem>
#endif

namespace zaidultra {

namespace {

#ifdef GEODE_IS_ANDROID
bool readNumber(std::filesystem::path const& path, double& value) {
    std::ifstream input(path);
    if (!input.good()) {
        return false;
    }
    input >> value;
    return input.good() || input.eof();
}

bool readText(std::filesystem::path const& path, std::string& value) {
    std::ifstream input(path);
    if (!input.good()) {
        return false;
    }
    std::getline(input, value);
    return !value.empty();
}

bool relevantThermalType(std::string type) {
    std::transform(type.begin(), type.end(), type.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    constexpr char const* keys[] = {
        "cpu", "gpu", "soc", "skin", "shell", "battery", "apss", "cluster"
    };
    for (auto key : keys) {
        if (type.find(key) != std::string::npos) {
            return true;
        }
    }
    return false;
}
#endif

} // namespace

AndroidGuardian& AndroidGuardian::get() {
    static AndroidGuardian instance;
    return instance;
}

void AndroidGuardian::reset() {
    m_snapshot = {};
#ifdef GEODE_IS_ANDROID
    m_snapshot.isAndroid = true;
#endif
    m_sinceRefresh = 999.f;
}

void AndroidGuardian::tick(float dt) {
    if (!std::isfinite(dt) || dt < 0.f || dt > 1.f) {
        return;
    }

    m_sinceRefresh += dt;
    m_snapshot.sampleAgeSeconds = m_sinceRefresh;
    if (m_sinceRefresh >= 1.f) {
        refresh();
    }
}

GuardianSnapshot AndroidGuardian::snapshot() const {
    return m_snapshot;
}

void AndroidGuardian::refresh() {
    m_sinceRefresh = 0.f;
    m_snapshot.sampleAgeSeconds = 0.f;

#ifndef GEODE_IS_ANDROID
    m_snapshot.isAndroid = false;
    m_snapshot.thermalAvailable = false;
    m_snapshot.cpuAvailable = false;
    m_snapshot.gpuAvailable = false;
    return;
#else
    m_snapshot.isAndroid = true;
    m_snapshot.thermalAvailable = false;
    m_snapshot.cpuAvailable = false;
    m_snapshot.gpuAvailable = false;
    m_snapshot.temperatureC = 0.f;
    m_snapshot.averageCpuMHz = 0.f;
    m_snapshot.gpuMHz = 0.f;

    std::error_code ec;
    const std::filesystem::path thermalRoot("/sys/class/thermal");
    if (std::filesystem::exists(thermalRoot, ec)) {
        float maxRelevantTemp = 0.f;
        for (std::filesystem::directory_iterator it(thermalRoot, ec), end; it != end && !ec; it.increment(ec)) {
            const auto name = it->path().filename().string();
            if (!name.starts_with("thermal_zone")) {
                continue;
            }

            std::string type;
            if (!readText(it->path() / "type", type) || !relevantThermalType(type)) {
                continue;
            }

            double raw = 0.0;
            if (!readNumber(it->path() / "temp", raw)) {
                continue;
            }

            // Android thermal drivers commonly expose milli-Celsius, while a
            // few expose Celsius directly. Normalize both and reject nonsense.
            double tempC = std::abs(raw) > 1000.0 ? raw / 1000.0 : raw;
            if (tempC < 0.0 || tempC > 130.0) {
                continue;
            }

            maxRelevantTemp = std::max(maxRelevantTemp, static_cast<float>(tempC));
            m_snapshot.thermalAvailable = true;
        }
        m_snapshot.temperatureC = maxRelevantTemp;
    }

    double cpuMHzSum = 0.0;
    int cpuSamples = 0;
    for (int cpu = 0; cpu < 32; ++cpu) {
        const auto path = std::filesystem::path("/sys/devices/system/cpu") /
            ("cpu" + std::to_string(cpu)) / "cpufreq" / "scaling_cur_freq";
        double rawKHz = 0.0;
        if (!readNumber(path, rawKHz) || rawKHz <= 0.0) {
            continue;
        }
        cpuMHzSum += rawKHz / 1000.0;
        ++cpuSamples;
    }
    if (cpuSamples > 0) {
        m_snapshot.cpuAvailable = true;
        m_snapshot.averageCpuMHz = static_cast<float>(cpuMHzSum / static_cast<double>(cpuSamples));
    }

    // Qualcomm / Adreno devices commonly expose the current GPU clock here.
    // The read is optional and gracefully stays unavailable on other devices.
    double gpuHz = 0.0;
    if (readNumber("/sys/class/kgsl/kgsl-3d0/gpuclk", gpuHz) && gpuHz > 0.0) {
        m_snapshot.gpuAvailable = true;
        m_snapshot.gpuMHz = static_cast<float>(gpuHz / 1000000.0);
    }
#endif
}

} // namespace zaidultra
