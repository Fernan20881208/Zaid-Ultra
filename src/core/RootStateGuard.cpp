#include "RootStateGuard.hpp"

#include "RootExecutor.hpp"
#include "Settings.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>

using namespace geode::prelude;

namespace zaid::ultra {
namespace {

constexpr auto kGoodixReportRate = "/sys/devices/platform/goodix_ts.0/switch_report_rate";
constexpr auto kSpeedTouch = "/sys/module/metis/parameters/speed_touch_enable";
constexpr auto kRecoveryFileName = "root-state-v1.txt";

std::filesystem::path recoveryPath() {
    return Mod::get()->getSaveDir() / kRecoveryFileName;
}

std::string capturedValue(RootStateGuard::SavedValue const& value) {
    if (!value.captured) {
        return "<no disponible>";
    }
    return value.wasPresent ? value.value : "<sin valor>";
}

std::string compactLower(std::string value) {
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char character) {
        return std::isspace(character) != 0;
    }), value.end());
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

} // namespace

RootStateGuard& RootStateGuard::get() {
    static RootStateGuard instance;
    return instance;
}

void RootStateGuard::recoverIfNeeded() {
    auto recoveryExists = std::filesystem::exists(recoveryPath());
    {
        std::lock_guard lock(m_mutex);
        m_status.recoveryPending = recoveryExists;
    }

    RootExecutor::get().post([this] {
        auto saved = loadRecoveryFile();
        if (!saved) {
            std::lock_guard lock(m_mutex);
            // An invalid snapshot must never be silently overwritten: its old
            // state is unknown, so the only safe behavior is to block writes.
            m_status.recoveryPending = std::filesystem::exists(recoveryPath());
            m_status.lastAction = m_status.recoveryPending
                ? "snapshot de recuperación inválido; cambios ROOT bloqueados"
                : "sin recuperación pendiente";
            return;
        }

        bool restored = restoreSnapshot(*saved);
        if (restored) {
            removeRecoveryFile();
        }
        {
            std::lock_guard lock(m_mutex);
            m_status.recoveryPending = !restored;
            m_status.lastAction = restored
                ? "estado anterior recuperado tras cierre inesperado"
                : "falló la recuperación; no se aplicarán cambios nuevos";
        }
        if (settings::diagnostics()) {
            if (restored) {
                log::info("Recovered and removed stale ROOT state snapshot");
            } else {
                log::error("Could not recover stale ROOT state snapshot");
            }
        }
    });
}

void RootStateGuard::begin(RootGuardConfig config) {
    RootExecutor::get().post([this, config] {
        if (std::filesystem::exists(recoveryPath())) {
            std::lock_guard lock(m_mutex);
            m_status.lastAction = "perfil omitido: recuperación pendiente";
            m_status.recoveryPending = true;
            return;
        }

        Snapshot snapshot;
        if (config.refreshRate) {
            snapshot.minRefresh = readSetting("system", "min_refresh_rate");
            snapshot.peakRefresh = readSetting("system", "peak_refresh_rate");
        }
        if (config.suppressHeadsUp) {
            snapshot.headsUp = readSetting("global", "heads_up_notifications_enabled");
        }
        if (config.touchBoost) {
            snapshot.goodix = readGoodixReportRate();
            snapshot.speedTouch = readNode(kSpeedTouch);
        }

        bool hasCapturedValue = snapshot.minRefresh.captured || snapshot.peakRefresh.captured ||
            snapshot.headsUp.captured || snapshot.goodix.captured || snapshot.speedTouch.captured;
        if (!hasCapturedValue) {
            updateStatusFromSnapshot(snapshot, false, "ROOT sin valores compatibles para aplicar");
            return;
        }

        // Never write a persistent/global value unless its exact prior state
        // is already durable for recovery on the next launch.
        if (!saveRecoveryFile(snapshot)) {
            updateStatusFromSnapshot(snapshot, false, "no se pudo guardar snapshot; cambios cancelados");
            return;
        }

        bool ok = true;
        if (snapshot.minRefresh.captured) {
            ok &= writeSetting("system", "min_refresh_rate", snapshot.minRefresh, "120.0");
        }
        if (snapshot.peakRefresh.captured) {
            ok &= writeSetting("system", "peak_refresh_rate", snapshot.peakRefresh, "120.0");
        }
        if (snapshot.headsUp.captured) {
            ok &= writeSetting("global", "heads_up_notifications_enabled", snapshot.headsUp, "0");
        }
        if (snapshot.goodix.captured) {
            ok &= writeGoodixReportRate(snapshot.goodix, "1");
        }
        if (snapshot.speedTouch.captured) {
            ok &= writeNode(kSpeedTouch, snapshot.speedTouch, "1");
        }

        {
            std::lock_guard lock(m_mutex);
            m_snapshot = snapshot;
        }
        updateStatusFromSnapshot(snapshot, true, ok ? "perfil ROOT aplicado" : "perfil parcial; restauración armada");

        if (settings::diagnostics()) {
            log::info(
                "ROOT state guard applied: refresh={} headsUp={} touch={} result={}",
                config.refreshRate,
                config.suppressHeadsUp,
                config.touchBoost,
                ok
            );
        }
    });
}

void RootStateGuard::end() {
    RootExecutor::get().post([this] {
        std::optional<Snapshot> snapshot;
        {
            std::lock_guard lock(m_mutex);
            if (m_status.active) {
                snapshot = m_snapshot;
            }
        }
        if (!snapshot) {
            snapshot = loadRecoveryFile();
        }
        if (!snapshot) {
            std::lock_guard lock(m_mutex);
            m_status.active = false;
            m_status.lastAction = "sin overrides ROOT activos";
            return;
        }

        bool restored = restoreSnapshot(*snapshot);
        if (restored) {
            removeRecoveryFile();
        }
        updateStatusFromSnapshot(*snapshot, !restored, restored ? "estado ROOT restaurado" : "falló restauración; snapshot conservado");

        if (settings::diagnostics()) {
            if (restored) {
                log::info("ROOT state guard restored every captured value");
            } else {
                log::error("ROOT state restore failed; recovery snapshot retained");
            }
        }
    });
}

void RootStateGuard::refreshReadOnlyStatus() {
    RootExecutor::get().post([this] {
        auto goodix = readGoodixReportRate();
        auto speedTouch = readNode(kSpeedTouch);
        auto minRefresh = readSetting("system", "min_refresh_rate");
        auto peakRefresh = readSetting("system", "peak_refresh_rate");
        auto headsUp = readSetting("global", "heads_up_notifications_enabled");

        std::lock_guard lock(m_mutex);
        m_status.goodix = goodixDisplay(goodix);
        m_status.speedTouch = capturedValue(speedTouch);
        m_status.minRefresh = capturedValue(minRefresh);
        m_status.peakRefresh = capturedValue(peakRefresh);
        m_status.headsUp = capturedValue(headsUp);
        m_status.lastAction = "diagnóstico ROOT actualizado";
    });
}

RootGuardStatus RootStateGuard::status() const {
    std::lock_guard lock(m_mutex);
    return m_status;
}

std::string RootStateGuard::trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    auto first = value.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? std::string{} : value.substr(first);
}

bool RootStateGuard::safeNumber(std::string const& value) {
    if (value.empty() || value.size() > 32) {
        return false;
    }
    bool digit = false;
    for (char c : value) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            digit = true;
        } else if (c != '.' && c != '+' && c != '-') {
            return false;
        }
    }
    return digit;
}

bool RootStateGuard::safeSwitch(std::string const& value) {
    return value == "0" || value == "1" || value == "Y" || value == "N" || value == "y" || value == "n";
}

RootStateGuard::SavedValue RootStateGuard::readSetting(char const* table, char const* key) {
    auto result = RootExecutor::get().runRoot(fmt::format("settings get {} {}", table, key));
    auto value = trim(result.output);
    if (!result.ok()) {
        return {};
    }
    if (value == "null") {
        return {true, false, {}};
    }
    if (!safeNumber(value)) {
        return {};
    }
    return {true, true, value};
}

RootStateGuard::SavedValue RootStateGuard::readNode(char const* path) {
    auto result = RootExecutor::get().runRoot(fmt::format(
        "if [ -r '{0}' ]; then cat '{0}'; else printf '__missing__'; fi",
        path
    ));
    auto value = trim(result.output);
    if (!result.ok() || value == "__missing__") {
        return {};
    }
    if (!safeSwitch(value)) {
        return {};
    }
    return {true, true, value};
}

RootStateGuard::SavedValue RootStateGuard::readGoodixReportRate() {
    auto result = RootExecutor::get().runRoot(fmt::format(
        "if [ -r '{0}' ]; then cat '{0}'; else printf '__missing__'; fi",
        kGoodixReportRate
    ));
    auto raw = trim(result.output);
    if (!result.ok() || raw == "__missing__") {
        return {};
    }

    // Goodix BERLIN 9916R exposes a write-only switch semantic through a
    // human-readable readback. These are the two values verified on duchamp:
    // writing 0 reports 240 Hz and writing 1 reports 480 Hz.
    auto normalized = compactLower(raw);
    if (normalized == "0" || normalized == "touchreportrate::240hz") {
        return {true, true, "0"};
    }
    if (normalized == "1" || normalized == "touchreportrate::480hz") {
        return {true, true, "1"};
    }
    return {};
}

std::string RootStateGuard::goodixDisplay(SavedValue const& value) {
    if (!value.captured) {
        return "<no disponible>";
    }
    if (value.value == "0") {
        return "240 Hz (0 verificado)";
    }
    if (value.value == "1") {
        return "480 Hz (1 verificado)";
    }
    return "<lectura desconocida>";
}

bool RootStateGuard::writeSetting(
    char const* table,
    char const* key,
    SavedValue const& original,
    std::optional<std::string> replacement
) {
    if (!original.captured) {
        return true;
    }
    std::string command;
    if (!replacement) {
        command = original.wasPresent
            ? fmt::format("settings put {} {} {}", table, key, original.value)
            : fmt::format("settings delete {} {} >/dev/null", table, key);
    } else {
        if (!safeNumber(*replacement)) {
            return false;
        }
        command = fmt::format("settings put {} {} {}", table, key, *replacement);
    }
    return RootExecutor::get().runRoot(command).ok();
}

bool RootStateGuard::writeNode(
    char const* path,
    SavedValue const& original,
    std::optional<std::string> replacement
) {
    if (!original.captured) {
        return true;
    }
    auto value = replacement ? *replacement : original.value;
    if (!safeSwitch(value)) {
        return false;
    }
    return RootExecutor::get().runRoot(fmt::format(
        "if [ -w '{0}' ]; then printf '%s\\n' '{1}' > '{0}'; else exit 20; fi",
        path,
        value
    )).ok();
}

bool RootStateGuard::writeGoodixReportRate(
    SavedValue const& original,
    std::optional<std::string> replacement
) {
    if (!original.captured) {
        return true;
    }
    auto value = replacement ? *replacement : original.value;
    if (value != "0" && value != "1") {
        return false;
    }
    auto write = RootExecutor::get().runRoot(fmt::format(
        "if [ -w '{0}' ]; then printf '%s\\n' '{1}' > '{0}'; else exit 20; fi",
        kGoodixReportRate,
        value
    ));
    if (!write.ok()) {
        return false;
    }
    auto verified = readGoodixReportRate();
    return verified.captured && verified.value == value;
}

bool RootStateGuard::restoreSnapshot(Snapshot const& snapshot) {
    bool ok = true;
    ok &= writeSetting("system", "min_refresh_rate", snapshot.minRefresh, std::nullopt);
    ok &= writeSetting("system", "peak_refresh_rate", snapshot.peakRefresh, std::nullopt);
    ok &= writeSetting("global", "heads_up_notifications_enabled", snapshot.headsUp, std::nullopt);
    ok &= writeGoodixReportRate(snapshot.goodix, std::nullopt);
    ok &= writeNode(kSpeedTouch, snapshot.speedTouch, std::nullopt);
    return ok;
}

bool RootStateGuard::saveRecoveryFile(Snapshot const& snapshot) {
    std::error_code error;
    std::filesystem::create_directories(recoveryPath().parent_path(), error);
    std::ofstream file(recoveryPath(), std::ios::trunc);
    if (!file.good()) {
        return false;
    }

    auto writeValue = [&file](char const* key, SavedValue const& value) {
        file << key << ".captured=" << (value.captured ? 1 : 0) << '\n';
        file << key << ".present=" << (value.wasPresent ? 1 : 0) << '\n';
        file << key << ".value=" << value.value << '\n';
    };

    file << "version=1\n";
    writeValue("min_refresh", snapshot.minRefresh);
    writeValue("peak_refresh", snapshot.peakRefresh);
    writeValue("heads_up", snapshot.headsUp);
    writeValue("goodix", snapshot.goodix);
    writeValue("speed_touch", snapshot.speedTouch);
    file.flush();
    return file.good();
}

std::optional<RootStateGuard::Snapshot> RootStateGuard::loadRecoveryFile() {
    std::ifstream file(recoveryPath());
    if (!file.good()) {
        return std::nullopt;
    }

    std::map<std::string, std::string> values;
    std::string line;
    while (std::getline(file, line)) {
        auto separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        values[line.substr(0, separator)] = line.substr(separator + 1);
    }
    if (values["version"] != "1") {
        return std::nullopt;
    }

    auto readValue = [&values](char const* key, bool numberOnly) -> SavedValue {
        auto prefix = std::string(key);
        SavedValue value;
        value.captured = values[prefix + ".captured"] == "1";
        value.wasPresent = values[prefix + ".present"] == "1";
        value.value = values[prefix + ".value"];
        if (!value.captured) {
            return value;
        }
        if (value.wasPresent && (numberOnly ? !safeNumber(value.value) : !safeSwitch(value.value))) {
            return {};
        }
        return value;
    };

    Snapshot snapshot;
    snapshot.minRefresh = readValue("min_refresh", true);
    snapshot.peakRefresh = readValue("peak_refresh", true);
    snapshot.headsUp = readValue("heads_up", true);
    snapshot.goodix = readValue("goodix", false);
    snapshot.speedTouch = readValue("speed_touch", false);
    return snapshot;
}

void RootStateGuard::removeRecoveryFile() {
    std::error_code error;
    std::filesystem::remove(recoveryPath(), error);
}

void RootStateGuard::updateStatusFromSnapshot(Snapshot const& snapshot, bool active, std::string action) {
    auto currentGoodix = readGoodixReportRate();
    auto currentSpeedTouch = readNode(kSpeedTouch);
    std::lock_guard lock(m_mutex);
    m_status.active = active;
    m_status.recoveryPending = std::filesystem::exists(recoveryPath());
    m_status.goodix = goodixDisplay(currentGoodix);
    m_status.speedTouch = capturedValue(currentSpeedTouch);
    m_status.minRefresh = snapshot.minRefresh.captured ? (active ? "120.0 (perfil)" : capturedValue(snapshot.minRefresh)) : "<no disponible>";
    m_status.peakRefresh = snapshot.peakRefresh.captured ? (active ? "120.0 (perfil)" : capturedValue(snapshot.peakRefresh)) : "<no disponible>";
    m_status.headsUp = snapshot.headsUp.captured ? (active ? "0 (perfil)" : capturedValue(snapshot.headsUp)) : "<no disponible>";
    m_status.lastAction = std::move(action);
}

} // namespace zaid::ultra
