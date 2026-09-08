#include "LatencyManager.hpp"

#include <Geode/Geode.hpp>

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#ifdef GEODE_IS_ANDROID
#include <EGL/egl.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

using namespace geode::prelude;

namespace zaid::ultra {
namespace {

constexpr auto kGoodixReportRate = "/sys/devices/platform/goodix_ts.0/switch_report_rate";
constexpr auto kSpeedTouch = "/sys/module/metis/parameters/speed_touch_enable";

bool setting(const char* key) {
    return Mod::get()->getSettingValue<bool>(key);
}

std::string readFirstLine(const char* path) {
    std::ifstream file(path);
    std::string line;
    if (!file.good() || !std::getline(file, line)) {
        return "<no disponible>";
    }
    return line;
}

#ifdef GEODE_IS_ANDROID
long currentThreadId() {
    return static_cast<long>(::syscall(SYS_gettid));
}
#endif

} // namespace

LatencyManager& LatencyManager::get() {
    static LatencyManager instance;
    return instance;
}

void LatencyManager::prime() {
#ifdef GEODE_IS_ANDROID
    if (setting("diagnostic-logs")) {
        log::info("Zaid-Ultra low-latency backend loaded on Android");
        logTouchStatus("carga");
    }
#else
    log::warn("Zaid-Ultra low-latency backend is Android-only");
#endif
}

void LatencyManager::onGameplayThread() {
#ifdef GEODE_IS_ANDROID
    const auto tid = currentThreadId();

    if (setting("diagnostic-logs")) {
        log::info("Gameplay thread detected: tid={}", tid);
    }

    applyThreadTuning();
    applyRenderTuning();
    queueRootTuning(tid);
#else
    return;
#endif
}

void LatencyManager::applyThreadTuning() {
#ifdef GEODE_IS_ANDROID
    const auto tid = currentThreadId();

    if (setting("minimum-timer-slack")) {
        errno = 0;
        const int result = ::prctl(PR_SET_TIMERSLACK, 1UL, 0UL, 0UL, 0UL);
        if (setting("diagnostic-logs")) {
            if (result == 0) {
                log::info("Timer slack del hilo de juego ajustado a 1 ns");
            } else {
                log::warn("No se pudo reducir timer slack (errno={})", errno);
            }
        }
    }

    if (setting("game-thread-priority")) {
        errno = 0;
        const int result = ::setpriority(PRIO_PROCESS, static_cast<id_t>(tid), -8);
        if (setting("diagnostic-logs")) {
            if (result == 0) {
                log::info("Prioridad del hilo de juego elevada directamente (nice=-8)");
            } else {
                log::debug("Android rechazó setpriority directo (errno={}); se intentará respaldo ROOT", errno);
            }
        }
    }
#endif
}

void LatencyManager::applyRenderTuning() {
#ifdef GEODE_IS_ANDROID
    if (!setting("experimental-no-vsync")) {
        return;
    }

    const EGLDisplay display = ::eglGetCurrentDisplay();
    if (display == EGL_NO_DISPLAY) {
        if (setting("diagnostic-logs")) {
            log::warn("No-VSync: no hay EGLDisplay actual");
        }
        return;
    }

    const EGLBoolean result = ::eglSwapInterval(display, 0);
    if (setting("diagnostic-logs")) {
        if (result == EGL_TRUE) {
            log::info("No-VSync experimental solicitado: EGL swap interval = 0");
        } else {
            log::warn("EGL rechazó swap interval 0 (error=0x{:x})", static_cast<unsigned>(::eglGetError()));
        }
    }
#endif
}

void LatencyManager::queueRootTuning(long gameThreadId) {
#ifdef GEODE_IS_ANDROID
    const bool touchBoost = setting("root-touch-boost");
    const bool priorityBoost = setting("game-thread-priority");

    if (!touchBoost && !priorityBoost) {
        return;
    }

    if (m_rootWorkRunning.exchange(true)) {
        return;
    }

    const bool diagnostics = setting("diagnostic-logs");

    std::thread([this, touchBoost, priorityBoost, diagnostics, gameThreadId] {
        std::ostringstream rootScript;

        if (touchBoost) {
            // These are deliberately limited to the two nodes already verified on duchamp.
            rootScript
                << "if [ -e " << kGoodixReportRate << " ]; then echo 1 > " << kGoodixReportRate << "; fi; "
                << "if [ -e " << kSpeedTouch << " ]; then echo 1 > " << kSpeedTouch << "; fi; ";
        }

        if (priorityBoost && gameThreadId > 0) {
            // nice=-10 is aggressive but still normal CFS scheduling, not realtime/FIFO.
            rootScript
                << "renice -n -10 -p " << gameThreadId << " >/dev/null 2>&1 || true; ";
        }

        const std::string command = "su -c '" + rootScript.str() + "'";
        const int result = std::system(command.c_str());

        if (diagnostics) {
            log::info("ROOT low-latency command finished with code {}", result);
            logTouchStatus("después de ROOT");
        }

        m_rootWorkRunning.store(false);
    }).detach();
#else
    (void)gameThreadId;
#endif
}

void LatencyManager::logTouchStatus(const char* stage) const {
#ifdef GEODE_IS_ANDROID
    if (!setting("diagnostic-logs")) {
        return;
    }

    log::info(
        "Touch status [{}]: Goodix='{}', speed_touch='{}'",
        stage,
        readFirstLine(kGoodixReportRate),
        readFirstLine(kSpeedTouch)
    );
#else
    (void)stage;
#endif
}

} // namespace zaid::ultra
