#pragma once

#include <mutex>
#include <optional>
#include <string>

namespace zaid::ultra {

struct RootGuardConfig final {
    bool touchBoost = false;
    bool refreshRate = false;
    bool suppressHeadsUp = false;
};

struct RootGuardStatus final {
    bool active = false;
    bool recoveryPending = false;
    std::string goodix = "<no disponible>";
    std::string speedTouch = "<no disponible>";
    std::string minRefresh = "<no disponible>";
    std::string peakRefresh = "<no disponible>";
    std::string headsUp = "<no disponible>";
    std::string lastAction = "sin ejecutar";
};

// Owns every persistent/global value changed by the mod. Values are captured
// before the first write, persisted for crash recovery, and restored in FIFO
// order when PlayLayer exits.
class RootStateGuard final {
public:
    static RootStateGuard& get();

    void recoverIfNeeded();
    void begin(RootGuardConfig config);
    void end();
    void refreshReadOnlyStatus();

    RootGuardStatus status() const;

    // Public only so the small serialization helpers in the implementation
    // can remain ordinary functions; callers should treat these as internal.
    struct SavedValue final {
        bool captured = false;
        bool wasPresent = false;
        std::string value;
    };

    struct Snapshot final {
        int version = 1;
        SavedValue minRefresh;
        SavedValue peakRefresh;
        SavedValue headsUp;
        SavedValue goodix;
        SavedValue speedTouch;
    };

private:

    RootStateGuard() = default;

    static std::string trim(std::string value);
    static bool safeNumber(std::string const& value);
    static bool safeSwitch(std::string const& value);
    static SavedValue readSetting(char const* table, char const* key);
    static SavedValue readNode(char const* path);
    static SavedValue readGoodixReportRate();
    static std::string goodixDisplay(SavedValue const& value);
    static bool writeSetting(char const* table, char const* key, SavedValue const& original, std::optional<std::string> replacement);
    static bool writeNode(char const* path, SavedValue const& original, std::optional<std::string> replacement);
    static bool writeGoodixReportRate(SavedValue const& original, std::optional<std::string> replacement);
    static bool restoreSnapshot(Snapshot const& snapshot);

    static bool saveRecoveryFile(Snapshot const& snapshot);
    static std::optional<Snapshot> loadRecoveryFile();
    static void removeRecoveryFile();

    void updateStatusFromSnapshot(Snapshot const& snapshot, bool active, std::string action);

    mutable std::mutex m_mutex;
    Snapshot m_snapshot;
    RootGuardStatus m_status;
};

} // namespace zaid::ultra
