#pragma once

#include <mutex>
#include <optional>
#include <string>

namespace zaid::ultra {

struct ForcedTouchStatus final {
    bool started = false;
    bool active = false;
    bool verified = false;
    bool restoreArmed = false;
    bool disabledUntilRestart = false;
    std::string goodix = "<no disponible>";
    std::string speedTouch = "<no disponible>";
    std::string lastAction = "pendiente";
};

// Owns the two device-validated touch switches for the lifetime of the game.
// Unlike PlayLayer profiles, this state is not restored between levels. Its
// original values remain durably recorded and are restored on a clean exit or
// by the safe-console restore action.
class ForcedTouchBoost final {
public:
    static ForcedTouchBoost& get();

    void start();
    void tick(float dt);
    void restoreUntilRestart();
    void restoreForExit();
    ForcedTouchStatus status() const;

private:
    struct Snapshot final {
        std::string goodix;
        std::string speedTouch;
    };

    ForcedTouchBoost() = default;
    ForcedTouchBoost(ForcedTouchBoost const&) = delete;
    ForcedTouchBoost& operator=(ForcedTouchBoost const&) = delete;

    void activateOnWorker();
    void verifyOnWorker();
    void restoreOnWorker(char const* successAction);

    static std::optional<Snapshot> readNodes();
    static bool writeAndVerify(Snapshot const& values);
    static bool saveSnapshot(Snapshot const& snapshot);
    static std::optional<Snapshot> loadSnapshot();
    static void removeSnapshot();
    static std::string trim(std::string value);
    static std::optional<std::string> normalizeGoodix(std::string value);
    static bool safeSwitch(std::string const& value);
    static bool switchEquivalent(std::string const& left, std::string const& right);
    static std::string goodixDisplay(std::string const& value);
    void updateStatus(
        std::optional<Snapshot> const& current,
        bool active,
        bool verified,
        std::string action
    );

    mutable std::mutex m_mutex;
    ForcedTouchStatus m_status;
    float m_elapsed = 0.0f;
    bool m_requested = false;
    bool m_checkQueued = false;
};

} // namespace zaid::ultra
