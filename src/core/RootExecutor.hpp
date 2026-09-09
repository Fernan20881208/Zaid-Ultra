#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace zaid::ultra {

struct CommandResult final {
    int exitCode = -1;
    std::string output;

    bool ok() const {
        return exitCode == 0;
    }
};

enum class RootAvailability {
    Unknown,
    Available,
    Unavailable,
};

// One serialized worker owns every `su` call. This keeps PlayLayer free of
// blocking shell work and guarantees that enter/restore jobs cannot overtake
// each other.
class RootExecutor final {
public:
    static RootExecutor& get();

    using Job = std::function<void()>;

    void start();
    void post(Job job);
    void probe();

    // Intended for code already running on the serialized worker. There is no
    // user-facing arbitrary command entry point in Zaid-Ultra.
    CommandResult runRoot(std::string const& script) const;

    RootAvailability availability() const;
    int rootUid() const;
    std::string lastError() const;

private:
    RootExecutor() = default;
    ~RootExecutor();
    RootExecutor(RootExecutor const&) = delete;
    RootExecutor& operator=(RootExecutor const&) = delete;

    void workerLoop();
    static std::string shellQuote(std::string const& value);

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<Job> m_jobs;
    std::thread m_worker;
    bool m_started = false;
    bool m_stopping = false;
    RootAvailability m_availability = RootAvailability::Unknown;
    int m_rootUid = -1;
    std::string m_lastError;
};

} // namespace zaid::ultra
