#include "ForcedTouchBoost.hpp"

#include "../core/RootExecutor.hpp"
#include "../core/RootStateGuard.hpp"
#include "../core/Settings.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <utility>

using namespace geode::prelude;

namespace zaid::ultra {
namespace {

constexpr char kGoodixReportRate[] = "/sys/devices/platform/goodix_ts.0/switch_report_rate";
constexpr char kSpeedTouch[] = "/sys/module/metis/parameters/speed_touch_enable";
constexpr char kRecoveryFileName[] = "forced-touch-state-v1.txt";
constexpr float kVerificationIntervalSeconds = 15.0f;

std::filesystem::path recoveryPath() {
    return Mod::get()->getSaveDir() / kRecoveryFileName;
}

} // namespace

ForcedTouchBoost& ForcedTouchBoost::get() {
    // Intentionally process-lifetime: queued ROOT jobs may still be draining
    // after Geometry Dash sends its exit event.
    static auto* instance = new ForcedTouchBoost();
    return *instance;
}

void ForcedTouchBoost::start() {
    {
        std::lock_guard lock(m_mutex);
        if (m_status.started || m_status.disabledUntilRestart) {
            return;
        }
        m_status.started = true;
        m_status.lastAction = "aplicación forzosa en cola";
        m_requested = true;
        m_checkQueued = true;
    }
    RootExecutor::get().post([this] {
        activateOnWorker();
        std::lock_guard lock(m_mutex);
        m_checkQueued = false;
    });
}

void ForcedTouchBoost::tick(float dt) {
    bool queueCheck = false;
    bool verifyActive = false;
    {
        std::lock_guard lock(m_mutex);
        if (!m_requested || m_status.disabledUntilRestart) {
            return;
        }
        m_elapsed += std::clamp(dt, 0.0f, 1.0f);
        if (m_elapsed >= kVerificationIntervalSeconds && !m_checkQueued) {
            m_elapsed = 0.0f;
            m_checkQueued = true;
            queueCheck = true;
            verifyActive = m_status.active;
        }
    }
    if (queueCheck) {
        RootExecutor::get().post([this, verifyActive] {
            if (verifyActive) {
                verifyOnWorker();
            } else {
                // ROOT grants and OEM nodes can become available shortly
                // after the menu appears. Retrying still follows the full
                // snapshot-before-write path in activateOnWorker().
                activateOnWorker();
            }
            std::lock_guard lock(m_mutex);
            m_checkQueued = false;
        });
    }
}

void ForcedTouchBoost::restoreUntilRestart() {
    {
        std::lock_guard lock(m_mutex);
        m_requested = false;
        m_status.disabledUntilRestart = true;
        m_status.lastAction = "restauración manual en cola";
    }
    RootExecutor::get().post([this] { restoreOnWorker("touch restaurado hasta reiniciar"); });
}

void ForcedTouchBoost::restoreForExit() {
    {
        std::lock_guard lock(m_mutex);
        if (!m_status.started) {
            return;
        }
        m_requested = false;
        m_status.lastAction = "restauración al salir en cola";
    }
    RootExecutor::get().post([this] { restoreOnWorker("touch restaurado al salir"); });
}

ForcedTouchStatus ForcedTouchBoost::status() const {
    std::lock_guard lock(m_mutex);
    return m_status;
}

void ForcedTouchBoost::activateOnWorker() {
    {
        std::lock_guard lock(m_mutex);
        if (!m_requested || m_status.disabledUntilRestart) {
            return;
        }
    }

    // The legacy guard is recovered first on the same serialized worker. If
    // its snapshot remains pending, its exact prior state is unknown and no
    // new persistent ROOT write is safe.
    if (RootStateGuard::get().status().recoveryPending) {
        updateStatus(std::nullopt, false, false, "recuperación ROOT pendiente; touch bloqueado");
        return;
    }

    if (std::filesystem::exists(recoveryPath())) {
        auto stale = loadSnapshot();
        if (!stale) {
            updateStatus(std::nullopt, false, false, "snapshot touch inválido; escrituras bloqueadas");
            return;
        }
        if (!writeAndVerify(*stale)) {
            updateStatus(readNodes(), false, false, "falló recuperación touch; snapshot conservado");
            return;
        }
        removeSnapshot();
    }

    auto original = readNodes();
    if (!original) {
        updateStatus(std::nullopt, false, false, "nodos Goodix/metis no compatibles");
        return;
    }
    if (!saveSnapshot(*original)) {
        updateStatus(original, false, false, "no se pudo armar restauración; cambios cancelados");
        return;
    }

    // This is the requested mandatory command, guarded only by exact-node
    // existence/writability checks and followed by semantic readback.
    Snapshot forced{.goodix = "1", .speedTouch = "1"};
    auto verified = writeAndVerify(forced);
    updateStatus(
        readNodes(),
        true,
        verified,
        verified ? "480 Hz + speed_touch forzados y verificados" : "aplicación touch parcial; restauración armada"
    );

    if (settings::diagnostics()) {
        log::info("Forced touch boost applied: verified={}", verified);
    }
}

void ForcedTouchBoost::verifyOnWorker() {
    bool shouldVerify = false;
    {
        std::lock_guard lock(m_mutex);
        shouldVerify = m_requested && !m_status.disabledUntilRestart;
    }

    auto current = shouldVerify ? readNodes() : std::optional<Snapshot>{};
    if (shouldVerify && !std::filesystem::exists(recoveryPath())) {
        {
            std::lock_guard lock(m_mutex);
            m_requested = false;
            m_status.disabledUntilRestart = true;
        }
        updateStatus(current, false, false, "snapshot touch ausente; escrituras bloqueadas");
        return;
    }
    bool verified = current && current->goodix == "1" &&
        switchEquivalent(current->speedTouch, "1");
    bool repaired = false;
    if (shouldVerify && !verified) {
        Snapshot forced{.goodix = "1", .speedTouch = "1"};
        repaired = writeAndVerify(forced);
        current = readNodes();
        verified = current && current->goodix == "1" &&
            switchEquivalent(current->speedTouch, "1");
    }

    if (!shouldVerify) {
        return;
    }
    updateStatus(
        current,
        true,
        verified,
        verified
            ? (repaired ? "driver cambió el modo; 480 Hz reaplicados" : "480 Hz + speed_touch verificados")
            : "no se pudo mantener el boost touch forzoso"
    );
}

void ForcedTouchBoost::restoreOnWorker(char const* successAction) {
    auto snapshot = loadSnapshot();
    if (!snapshot) {
        updateStatus(readNodes(), false, false, "sin snapshot touch que restaurar");
        return;
    }
    auto restored = writeAndVerify(*snapshot);
    if (restored) {
        removeSnapshot();
    }
    updateStatus(
        readNodes(),
        !restored,
        false,
        restored ? std::string(successAction) : "falló restauración touch; snapshot conservado"
    );
}

std::optional<ForcedTouchBoost::Snapshot> ForcedTouchBoost::readNodes() {
    auto result = RootExecutor::get().runRoot(fmt::format(
        "if [ ! -r '{0}' ] || [ ! -r '{1}' ]; then exit 20; fi; "
        "printf 'GOODIX='; cat '{0}'; printf '\\nSPEED='; cat '{1}'; printf '\\n'",
        kGoodixReportRate,
        kSpeedTouch
    ));
    if (!result.ok()) {
        return std::nullopt;
    }

    auto goodixStart = result.output.find("GOODIX=");
    auto speedStart = result.output.find("SPEED=");
    if (goodixStart == std::string::npos || speedStart == std::string::npos || speedStart <= goodixStart) {
        return std::nullopt;
    }
    goodixStart += 7;
    auto rawGoodix = result.output.substr(goodixStart, speedStart - goodixStart);
    auto rawSpeed = trim(result.output.substr(speedStart + 6));
    auto goodix = normalizeGoodix(std::move(rawGoodix));
    if (!goodix || !safeSwitch(rawSpeed)) {
        return std::nullopt;
    }
    return Snapshot{.goodix = *goodix, .speedTouch = rawSpeed};
}

bool ForcedTouchBoost::writeAndVerify(Snapshot const& values) {
    if ((values.goodix != "0" && values.goodix != "1") || !safeSwitch(values.speedTouch)) {
        return false;
    }
    auto write = RootExecutor::get().runRoot(fmt::format(
        "if [ ! -w '{0}' ] || [ ! -w '{1}' ]; then exit 20; fi; "
        "printf '{2}\\n' > '{0}' || exit 21; "
        "printf '{3}\\n' > '{1}' || exit 22",
        kGoodixReportRate,
        kSpeedTouch,
        values.goodix,
        values.speedTouch
    ));
    if (!write.ok()) {
        return false;
    }
    auto current = readNodes();
    return current && current->goodix == values.goodix &&
        switchEquivalent(current->speedTouch, values.speedTouch);
}

bool ForcedTouchBoost::saveSnapshot(Snapshot const& snapshot) {
    if ((snapshot.goodix != "0" && snapshot.goodix != "1") || !safeSwitch(snapshot.speedTouch)) {
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(recoveryPath().parent_path(), error);
    if (error) {
        return false;
    }
    std::ofstream file(recoveryPath(), std::ios::trunc);
    file << "version=1\n";
    file << "goodix=" << snapshot.goodix << '\n';
    file << "speed_touch=" << snapshot.speedTouch << '\n';
    file.flush();
    return file.good();
}

std::optional<ForcedTouchBoost::Snapshot> ForcedTouchBoost::loadSnapshot() {
    std::ifstream file(recoveryPath());
    if (!file.good()) {
        return std::nullopt;
    }
    std::map<std::string, std::string> values;
    std::string line;
    while (std::getline(file, line)) {
        auto separator = line.find('=');
        if (separator != std::string::npos) {
            values[line.substr(0, separator)] = line.substr(separator + 1);
        }
    }
    auto goodix = trim(values["goodix"]);
    auto speedTouch = trim(values["speed_touch"]);
    if (values["version"] != "1" || (goodix != "0" && goodix != "1") || !safeSwitch(speedTouch)) {
        return std::nullopt;
    }
    return Snapshot{.goodix = goodix, .speedTouch = speedTouch};
}

void ForcedTouchBoost::removeSnapshot() {
    std::error_code error;
    std::filesystem::remove(recoveryPath(), error);
}

std::string ForcedTouchBoost::trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    auto first = value.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? std::string{} : value.substr(first);
}

std::optional<std::string> ForcedTouchBoost::normalizeGoodix(std::string value) {
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char character) {
        return std::isspace(character) != 0;
    }), value.end());
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (value == "0" || value == "touchreportrate::240hz") {
        return "0";
    }
    if (value == "1" || value == "touchreportrate::480hz") {
        return "1";
    }
    return std::nullopt;
}

bool ForcedTouchBoost::safeSwitch(std::string const& value) {
    return value == "0" || value == "1" || value == "Y" || value == "N" ||
        value == "y" || value == "n";
}

bool ForcedTouchBoost::switchEquivalent(std::string const& left, std::string const& right) {
    if (!safeSwitch(left) || !safeSwitch(right)) {
        return false;
    }
    auto enabled = [](std::string const& value) {
        return value == "1" || value == "Y" || value == "y";
    };
    return enabled(left) == enabled(right);
}

std::string ForcedTouchBoost::goodixDisplay(std::string const& value) {
    if (value == "1") {
        return "480 Hz (1 verificado)";
    }
    if (value == "0") {
        return "240 Hz (0 verificado)";
    }
    return "<lectura desconocida>";
}

void ForcedTouchBoost::updateStatus(
    std::optional<Snapshot> const& current,
    bool active,
    bool verified,
    std::string action
) {
    std::lock_guard lock(m_mutex);
    m_status.active = active;
    m_status.verified = verified;
    m_status.restoreArmed = std::filesystem::exists(recoveryPath());
    m_status.goodix = current ? goodixDisplay(current->goodix) : "<no disponible>";
    m_status.speedTouch = current ? current->speedTouch : "<no disponible>";
    m_status.lastAction = std::move(action);
}

} // namespace zaid::ultra
