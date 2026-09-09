#include "RootExecutor.hpp"

#include "Settings.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <sys/wait.h>

using namespace geode::prelude;

namespace zaid::ultra {
namespace {

constexpr std::size_t kMaximumCapturedOutput = 64 * 1024;

std::string trim(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    auto first = value.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? std::string{} : value.substr(first);
}

} // namespace

RootExecutor& RootExecutor::get() {
    static RootExecutor instance;
    return instance;
}

RootExecutor::~RootExecutor() {
    {
        std::lock_guard lock(m_mutex);
        m_stopping = true;
    }
    m_cv.notify_all();
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

void RootExecutor::start() {
    std::lock_guard lock(m_mutex);
    if (m_started) {
        return;
    }
    m_started = true;
    m_worker = std::thread([this] { workerLoop(); });
}

void RootExecutor::post(Job job) {
    start();
    {
        std::lock_guard lock(m_mutex);
        if (m_stopping) {
            return;
        }
        m_jobs.emplace_back(std::move(job));
    }
    m_cv.notify_one();
}

void RootExecutor::probe() {
    post([this] {
        auto result = runRoot("id -u");
        auto value = trim(result.output);
        int uid = -1;
        if (!value.empty()) {
            char* end = nullptr;
            auto parsed = std::strtol(value.c_str(), &end, 10);
            if (end != value.c_str() && *end == '\0') {
                uid = static_cast<int>(parsed);
            }
        }

        {
            std::lock_guard lock(m_mutex);
            m_rootUid = uid;
            m_availability = result.ok() && uid == 0
                ? RootAvailability::Available
                : RootAvailability::Unavailable;
            m_lastError = m_availability == RootAvailability::Available
                ? std::string{}
                : fmt::format("su exit={} output={}", result.exitCode, value);
        }

        if (settings::diagnostics()) {
            if (uid == 0 && result.ok()) {
                log::info("ROOT probe: available (su id -u = 0)");
            } else {
                log::warn("ROOT probe failed: exit={} output='{}'", result.exitCode, value);
            }
        }
    });
}

CommandResult RootExecutor::runRoot(std::string const& script) const {
    CommandResult result;
    auto command = "su -c " + shellQuote(script) + " 2>&1";
    auto* pipe = ::popen(command.c_str(), "r");
    if (!pipe) {
        result.output = "popen failed";
        return result;
    }

    std::array<char, 1024> buffer{};
    while (result.output.size() < kMaximumCapturedOutput && std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        auto remaining = kMaximumCapturedOutput - result.output.size();
        result.output.append(buffer.data(), std::min(remaining, std::char_traits<char>::length(buffer.data())));
    }

    auto status = ::pclose(pipe);
    if (status == -1) {
        result.exitCode = -1;
    } else if (WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exitCode = 128 + WTERMSIG(status);
    }
    return result;
}

RootAvailability RootExecutor::availability() const {
    std::lock_guard lock(m_mutex);
    return m_availability;
}

int RootExecutor::rootUid() const {
    std::lock_guard lock(m_mutex);
    return m_rootUid;
}

std::string RootExecutor::lastError() const {
    std::lock_guard lock(m_mutex);
    return m_lastError;
}

void RootExecutor::workerLoop() {
    for (;;) {
        Job job;
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, [this] { return m_stopping || !m_jobs.empty(); });
            if (m_stopping && m_jobs.empty()) {
                return;
            }
            job = std::move(m_jobs.front());
            m_jobs.pop_front();
        }

        try {
            job();
        } catch (std::exception const& error) {
            log::error("ROOT worker job failed: {}", error.what());
        } catch (...) {
            log::error("ROOT worker job failed with an unknown exception");
        }
    }
}

std::string RootExecutor::shellQuote(std::string const& value) {
    std::string quoted{"'"};
    for (char character : value) {
        if (character == '\'') {
            quoted += "'\\''";
        } else {
            quoted += character;
        }
    }
    quoted += '\'';
    return quoted;
}

} // namespace zaid::ultra
