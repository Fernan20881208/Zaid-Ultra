#pragma once

#include <mutex>
#include <string>

namespace zaid::ultra {

struct ReplayProbeStatus final {
    bool pending = false;
    bool screenrecordAvailable = false;
    bool h264Output = false;
    bool displaySelection = false;
    bool audioOption = false;
    int exitCode = -1;
    std::string summary = "sin investigar en este dispositivo";
};

// This phase is intentionally diagnostic-only. The OEM screenrecord command
// line must be known before a continuous ROOT recorder is implemented.
class ReplayProbe final {
public:
    static ReplayProbe& get();

    void run();
    ReplayProbeStatus status() const;
    std::string details() const;

private:
    ReplayProbe() = default;

    mutable std::mutex m_mutex;
    ReplayProbeStatus m_status;
    std::string m_details;
};

} // namespace zaid::ultra
