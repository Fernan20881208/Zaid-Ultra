#pragma once

namespace zaidultra {

struct GuardianSnapshot {
    bool isAndroid = false;
    bool thermalAvailable = false;
    bool cpuAvailable = false;
    bool gpuAvailable = false;
    float temperatureC = 0.f;
    float averageCpuMHz = 0.f;
    float gpuMHz = 0.f;
    float sampleAgeSeconds = 0.f;
};

class AndroidGuardian final {
public:
    static AndroidGuardian& get();

    void reset();
    void tick(float dt);
    [[nodiscard]] GuardianSnapshot snapshot() const;

private:
    void refresh();

    GuardianSnapshot m_snapshot{};
    float m_sinceRefresh = 999.f;
};

} // namespace zaidultra
