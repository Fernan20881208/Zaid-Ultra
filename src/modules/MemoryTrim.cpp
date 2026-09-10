#include "MemoryTrim.hpp"

#include "../core/Settings.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <chrono>
#include <dlfcn.h>
#include <fstream>
#include <sstream>

using namespace geode::prelude;

namespace zaid::ultra {
namespace {

constexpr std::int64_t kMinimumUsefulReclaimKiB = 1024;

using MallocTrimFunction = int (*)(std::size_t);

MallocTrimFunction resolveMallocTrim() {
#ifdef GEODE_IS_ANDROID
    return reinterpret_cast<MallocTrimFunction>(::dlsym(RTLD_DEFAULT, "malloc_trim"));
#else
    return nullptr;
#endif
}

} // namespace

MemoryTrim& MemoryTrim::get() {
    static MemoryTrim instance;
    return instance;
}

void MemoryTrim::beforeGameplay(bool requested) {
    if (!requested) {
        return;
    }
    {
        std::lock_guard lock(m_mutex);
        if (m_status.autoDisabled) {
            return;
        }
    }
    execute(false);
}

void MemoryTrim::runNow() {
    execute(true);
}

MemoryTrimStatus MemoryTrim::status() const {
    std::lock_guard lock(m_mutex);
    return m_status;
}

void MemoryTrim::execute(bool manual) {
    bool alreadyAutoDisabled = false;
    {
        std::lock_guard lock(m_mutex);
        alreadyAutoDisabled = m_status.autoDisabled;
    }
    auto function = resolveMallocTrim();
    MemoryTrimStatus result;
    result.supported = function != nullptr;
    result.tested = true;
    if (!function) {
        result.note = "malloc_trim no disponible; no se aplicó ningún sustituto placebo";
        std::lock_guard lock(m_mutex);
        m_status = std::move(result);
        return;
    }

    result.rssBeforeKiB = readRssKiB();
    auto start = std::chrono::steady_clock::now();
    auto released = function(0);
    auto end = std::chrono::steady_clock::now();
    result.rssAfterKiB = readRssKiB();
    result.durationMs = std::chrono::duration<double, std::milli>(end - start).count();
    if (result.rssBeforeKiB > 0 && result.rssAfterKiB > 0) {
        result.reclaimedKiB = std::max<std::int64_t>(0, result.rssBeforeKiB - result.rssAfterKiB);
    }
    result.beneficial = result.reclaimedKiB >= kMinimumUsefulReclaimKiB;
    result.autoDisabled = alreadyAutoDisabled || (!manual && !result.beneficial);
    result.note = fmt::format(
        "malloc_trim={} RSS {} -> {} KiB, recuperado={} KiB, {:.2f} ms{}",
        released,
        result.rssBeforeKiB,
        result.rssAfterKiB,
        result.reclaimedKiB,
        result.durationMs,
        result.autoDisabled ? "; repetición automática desactivada" : ""
    );

    {
        std::lock_guard lock(m_mutex);
        m_status = result;
    }
    if (settings::diagnostics()) {
        log::info("Memory trim: {}", result.note);
    }
}

std::int64_t MemoryTrim::readRssKiB() {
    std::ifstream status("/proc/self/status");
    std::string key;
    while (status >> key) {
        if (key == "VmRSS:") {
            std::int64_t value = 0;
            status >> value;
            return value;
        }
        std::string rest;
        std::getline(status, rest);
    }
    return 0;
}

} // namespace zaid::ultra
