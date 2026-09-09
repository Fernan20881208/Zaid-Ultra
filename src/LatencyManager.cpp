#include "LatencyManager.hpp"

#include "core/RootExecutor.hpp"
#include "core/Settings.hpp"

#include <Geode/Geode.hpp>

#include <cerrno>

#ifdef GEODE_IS_ANDROID
#include <EGL/egl.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

using namespace geode::prelude;

namespace zaid::ultra {

LatencyManager& LatencyManager::get() {
    static LatencyManager instance;
    return instance;
}

void LatencyManager::prime() {
#ifdef GEODE_IS_ANDROID
    if (settings::diagnostics()) {
        log::info("Zaid-Ultra reversible latency backend loaded on Android64");
    }
#else
    log::warn("Zaid-Ultra low-latency backend is Android-only");
#endif
}

void LatencyManager::begin() {
#ifdef GEODE_IS_ANDROID
    // Defensive: PlayLayer can be recreated by restart/replay flows.
    end();

    ThreadTuningStatus status;
    status.active = true;
    status.threadId = currentThreadId();

    errno = 0;
    auto timerSlack = ::prctl(PR_GET_TIMERSLACK, 0UL, 0UL, 0UL, 0UL);
    if (timerSlack >= 0) {
        status.originalTimerSlackNs = timerSlack;
        status.timerSlackCaptured = true;
    }

    errno = 0;
    auto originalNice = ::getpriority(PRIO_PROCESS, static_cast<id_t>(status.threadId));
    if (errno == 0) {
        status.originalNice = originalNice;
        status.niceCaptured = true;
    }

    if (settings::enabled("minimum-timer-slack") && status.timerSlackCaptured) {
        status.timerSlackApplied = ::prctl(PR_SET_TIMERSLACK, 1UL, 0UL, 0UL, 0UL) == 0;
    }

    if (settings::enabled("game-thread-priority") && status.niceCaptured) {
        // Never lower a priority that another mod or the launcher already set
        // above our target.
        if (status.originalNice > -8) {
            status.priorityApplied = ::setpriority(
                PRIO_PROCESS,
                static_cast<id_t>(status.threadId),
                -8
            ) == 0;
            if (!status.priorityApplied) {
                status.priorityRootFallback = true;
            }
        }
    }

    if (settings::enabled("experimental-no-vsync")) {
        auto display = ::eglGetCurrentDisplay();
        status.noVsyncApplied = display != EGL_NO_DISPLAY &&
            ::eglSwapInterval(display, 0) == EGL_TRUE;
    }

    status.lastAction = fmt::format(
        "perfil local aplicado: tid={} slack={} nice={}{}",
        status.threadId,
        status.timerSlackApplied ? "1ns" : "sin cambio",
        status.priorityApplied ? "-8" : (status.priorityRootFallback ? "ROOT pendiente" : "sin cambio"),
        status.noVsyncApplied ? " vsync=0 experimental" : ""
    );
    {
        std::lock_guard lock(m_mutex);
        m_status = status;
    }
    // Publish the captured state before the asynchronous completion can update
    // it. This also lets end() enqueue restoration behind this exact job.
    if (status.priorityRootFallback) {
        queuePriorityChange(status.threadId, -8, false);
    }
    if (settings::diagnostics()) {
        log::info("{}", status.lastAction);
    }
#endif
}

void LatencyManager::end() {
#ifdef GEODE_IS_ANDROID
    ThreadTuningStatus status;
    {
        std::lock_guard lock(m_mutex);
        if (!m_status.active) {
            return;
        }
        status = m_status;
        m_status.active = false;
    }

    bool slackRestored = true;
    if (status.timerSlackApplied && status.timerSlackCaptured) {
        slackRestored = ::prctl(
            PR_SET_TIMERSLACK,
            static_cast<unsigned long>(status.originalTimerSlackNs),
            0UL,
            0UL,
            0UL
        ) == 0;
    }

    bool niceRestored = true;
    if (status.priorityRootFallback && status.niceCaptured) {
        // The apply job may still be pending. Always enqueue a matching restore
        // on the same FIFO worker; a direct restore here could otherwise run
        // first and then be overwritten by the late apply job.
        queuePriorityChange(status.threadId, status.originalNice, true);
        niceRestored = false;
    } else if (status.priorityApplied && status.niceCaptured) {
        niceRestored = ::setpriority(
            PRIO_PROCESS,
            static_cast<id_t>(status.threadId),
            status.originalNice
        ) == 0;
        if (!niceRestored) {
            queuePriorityChange(status.threadId, status.originalNice, true);
        }
    }

    bool vsyncRestored = true;
    if (status.noVsyncApplied) {
        // EGL exposes no getter for the prior interval. GD's normal Android
        // path uses interval 1, so this is the documented best-effort restore
        // for the opt-in experimental toggle.
        auto display = ::eglGetCurrentDisplay();
        vsyncRestored = display != EGL_NO_DISPLAY &&
            ::eglSwapInterval(display, 1) == EGL_TRUE;
    }

    {
        std::lock_guard lock(m_mutex);
        m_status.lastAction = fmt::format(
            "perfil local restaurado: slack={} nice={} vsync={}",
            slackRestored,
            niceRestored ? "sí" : "ROOT pendiente",
            vsyncRestored
        );
    }
    if (settings::diagnostics()) {
        log::info("Local latency state restored (slack={}, nice={}, vsync={})", slackRestored, niceRestored, vsyncRestored);
    }
#endif
}

ThreadTuningStatus LatencyManager::status() const {
    std::lock_guard lock(m_mutex);
    return m_status;
}

long LatencyManager::currentThreadId() {
#ifdef GEODE_IS_ANDROID
    return static_cast<long>(::syscall(SYS_gettid));
#else
    return -1;
#endif
}

void LatencyManager::queuePriorityChange(long threadId, int nice, bool restoring) {
#ifdef GEODE_IS_ANDROID
    if (threadId <= 0 || nice < -20 || nice > 19) {
        return;
    }
    RootExecutor::get().post([this, threadId, nice, restoring] {
        auto result = RootExecutor::get().runRoot(fmt::format(
            "renice -n {} -p {} >/dev/null",
            nice,
            threadId
        ));
        {
            std::lock_guard lock(m_mutex);
            if (!restoring && result.ok()) {
                m_status.priorityApplied = true;
            }
            m_status.lastAction = result.ok()
                ? (restoring ? "prioridad del hilo restaurada mediante ROOT" : "nice=-8 aplicado mediante ROOT")
                : fmt::format("renice ROOT falló (exit={})", result.exitCode);
        }
        if (settings::diagnostics()) {
            log::info("ROOT renice tid={} nice={} exit={}", threadId, nice, result.exitCode);
        }
    });
#else
    (void)threadId;
    (void)nice;
    (void)restoring;
#endif
}

} // namespace zaid::ultra
