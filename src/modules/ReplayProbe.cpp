#include "ReplayProbe.hpp"

#include "../core/RootExecutor.hpp"
#include "../core/Settings.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cctype>

using namespace geode::prelude;

namespace zaid::ultra {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

} // namespace

ReplayProbe& ReplayProbe::get() {
    static ReplayProbe instance;
    return instance;
}

void ReplayProbe::run() {
    {
        std::lock_guard lock(m_mutex);
        if (m_status.pending) {
            return;
        }
        m_status.pending = true;
        m_status.summary = "consultando screenrecord --help...";
    }

    RootExecutor::get().post([this] {
        // Read-only capability probe. No recording starts here.
        auto result = RootExecutor::get().runRoot(
            "command -v screenrecord >/dev/null 2>&1 || exit 127; screenrecord --help"
        );
        auto help = lower(result.output);
        ReplayProbeStatus status;
        status.pending = false;
        status.exitCode = result.exitCode;
        status.screenrecordAvailable = result.exitCode != 127 && !help.empty();
        status.h264Output = help.find("h264") != std::string::npos ||
            help.find("output-format") != std::string::npos;
        status.displaySelection = help.find("display-id") != std::string::npos ||
            help.find("physical-display-id") != std::string::npos;
        status.audioOption = help.find("audio") != std::string::npos;
        status.summary = status.screenrecordAvailable
            ? fmt::format(
                "screenrecord sí; h264={} display-id={} audio={} (exit={})",
                status.h264Output,
                status.displaySelection,
                status.audioOption,
                status.exitCode
            )
            : fmt::format("screenrecord no disponible (exit={})", status.exitCode);
        {
            std::lock_guard lock(m_mutex);
            m_status = status;
            m_details = result.output;
        }
        if (settings::diagnostics()) {
            log::info("Instant Replay capability: {}", status.summary);
            // The exact OEM help is required to design compatible flags. It
            // is bounded by RootExecutor and appears only in the local Geode
            // diagnostic log when the user explicitly runs the probe.
            log::debug("screenrecord --help output:\n{}", result.output);
        }
    });
}

ReplayProbeStatus ReplayProbe::status() const {
    std::lock_guard lock(m_mutex);
    return m_status;
}

std::string ReplayProbe::details() const {
    std::lock_guard lock(m_mutex);
    return m_details;
}

} // namespace zaid::ultra
