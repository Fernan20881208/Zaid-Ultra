#include "RootConsolePopup.hpp"

#include "../LatencyManager.hpp"
#include "../core/RootExecutor.hpp"
#include "../core/RootStateGuard.hpp"
#include "../core/SessionManager.hpp"
#include "../modules/AudioLatency.hpp"
#include "../modules/MemoryTrim.hpp"
#include "../modules/ReplayProbe.hpp"
#include "../modules/TelemetryManager.hpp"
#include "../platform/AndroidBridge.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/TextArea.hpp>

#include <fstream>
#include <string>

#ifdef GEODE_IS_ANDROID
#include <sys/types.h>
#include <unistd.h>
#endif

using namespace geode::prelude;

namespace zaid::ultra {
namespace {

std::string processName() {
    std::ifstream file("/proc/self/cmdline", std::ios::binary);
    std::string value;
    std::getline(file, value, '\0');
    return value.empty() ? "<no disponible>" : value;
}

std::string rootName(RootAvailability availability) {
    switch (availability) {
        case RootAvailability::Available: return "sí";
        case RootAvailability::Unavailable: return "no";
        case RootAvailability::Unknown: return "comprobando";
    }
    return "desconocido";
}

CCMenuItemSpriteExtra* actionButton(
    char const* label,
    CCObject* target,
    SEL_MenuHandler selector,
    char const* background = "GJ_button_01.png"
) {
    auto sprite = ButtonSprite::create(label, "bigFont.fnt", background, 0.7f);
    sprite->setScale(0.52f);
    return CCMenuItemSpriteExtra::create(sprite, target, selector);
}

} // namespace

RootConsolePopup* RootConsolePopup::create() {
    auto* result = new RootConsolePopup();
    if (result && result->init()) {
        result->autorelease();
        return result;
    }
    delete result;
    return nullptr;
}

bool RootConsolePopup::init() {
    if (!Popup::init(445.0f, 285.0f)) {
        return false;
    }
    this->setTitle("Zaid-Ultra | Consola segura", "goldFont.fnt", 0.62f, 20.0f);

    m_text = SimpleTextArea::create("Cargando...", "chatFont.fnt", 0.40f, 410.0f);
    m_text->setAlignment(kCCTextAlignmentLeft);
    m_text->setWrappingMode(WrappingMode::WORD_WRAP);
    m_text->setMaxLines(22);
    m_mainLayer->addChildAtPosition(m_text, Anchor::Center, {0.0f, 9.0f});

    m_buttonMenu->addChildAtPosition(
        actionButton("Actualizar", this, menu_selector(RootConsolePopup::onRefresh)),
        Anchor::Bottom,
        {-142.0f, 26.0f}
    );
    m_buttonMenu->addChildAtPosition(
        actionButton("Trim propio", this, menu_selector(RootConsolePopup::onTrim)),
        Anchor::Bottom,
        {-48.0f, 26.0f}
    );
    m_buttonMenu->addChildAtPosition(
        actionButton("Restaurar", this, menu_selector(RootConsolePopup::onRestore), "GJ_button_06.png"),
        Anchor::Bottom,
        {48.0f, 26.0f}
    );
    m_buttonMenu->addChildAtPosition(
        actionButton("Replay diag", this, menu_selector(RootConsolePopup::onReplayProbe)),
        Anchor::Bottom,
        {142.0f, 26.0f}
    );

    this->scheduleUpdate();
    refreshText();
    return true;
}

void RootConsolePopup::update(float dt) {
    m_elapsed += dt;
    if (m_elapsed >= 0.5f) {
        m_elapsed = 0.0f;
        refreshText();
    }
}

void RootConsolePopup::refreshText() {
    if (!m_text) {
        return;
    }
    auto root = RootExecutor::get().availability();
    auto rootState = RootStateGuard::get().status();
    auto display = AndroidBridge::get().displayStatus();
    auto thread = LatencyManager::get().status();
    auto telemetry = TelemetryManager::get().snapshot();
    auto memory = MemoryTrim::get().status();
    auto audio = AudioLatency::get().status();
    auto replay = ReplayProbe::get().status();

#ifdef GEODE_IS_ANDROID
    auto uid = static_cast<int>(::getuid());
    auto pid = static_cast<int>(::getpid());
#else
    int uid = -1;
    int pid = -1;
#endif

    auto text = fmt::format(
        "ROOT: {} | su id -u={} | UID/PID={}/{}\n"
        "Proceso: {}\n"
        "Sesión: {} | gameplay tid={}\n"
        "Goodix={} | speed_touch={}\n"
        "Pantalla={:.1f} Hz (solicitud={:.0f}, modos={})\n"
        "ROOT refresh min={} peak={} | heads-up={}\n"
        "CPU {:.0f}/{:.0f} MHz {:.1f}C | GPU {:.0f}/{:.0f} MHz {:.1f}C\n"
        "Batería {:.1f}C | throttle={} ({})\n"
        "FPS {:.0f} | frame {:.2f}/P95 {:.2f} ms | touch app {:.0f} Hz\n"
        "Input dispatch {:.2f} ms | hasta física {:.2f} ms | jitter {:.2f} ms\n"
        "Hilo: {}\n"
        "Trim: {}\n"
        "Audio: {} {} Hz DSP {}x{} (~{:.2f} ms), Android {}/{}\n"
        "Replay 60 s: diagnóstico previo | {}",
        rootName(root),
        RootExecutor::get().rootUid(),
        uid,
        pid,
        processName(),
        SessionManager::get().statusLine(),
        thread.threadId,
        rootState.goodix,
        rootState.speedTouch,
        display.currentRefreshHz,
        display.requestedRefreshHz,
        display.supportedModes,
        rootState.minRefresh,
        rootState.peakRefresh,
        rootState.headsUp,
        telemetry.cpuCurrentMHz,
        telemetry.cpuMaximumMHz,
        telemetry.cpuTemperatureC,
        telemetry.gpuCurrentMHz,
        telemetry.gpuMaximumMHz,
        telemetry.gpuTemperatureC,
        telemetry.batteryTemperatureC,
        telemetry.throttling ? "sí" : "no",
        telemetry.throttlingReason,
        telemetry.fps,
        telemetry.frameTimeMs,
        telemetry.frameTimeP95Ms,
        telemetry.touchDeliveryHz,
        telemetry.inputDispatchAgeMs,
        telemetry.inputToPhysicsMs,
        telemetry.touchJitterMs,
        thread.lastAction,
        memory.note,
        audio.output,
        audio.softwareSampleRate,
        audio.dspBlockFrames,
        audio.dspBufferCount,
        audio.nominalMixQueueMs,
        audio.androidSampleRate,
        audio.androidFramesPerBuffer,
        replay.summary
    );
    m_text->setText(std::move(text));
}

void RootConsolePopup::onRefresh(CCObject*) {
    RootExecutor::get().probe();
    RootStateGuard::get().refreshReadOnlyStatus();
    AndroidBridge::get().sampleRefreshRate();
    AudioLatency::get().refreshDiagnostics();
    TelemetryManager::get().requestSample();
    Notification::create("Diagnósticos actualizándose", NotificationIcon::Info)->show();
}

void RootConsolePopup::onTrim(CCObject*) {
    MemoryTrim::get().runNow();
    refreshText();
    auto result = MemoryTrim::get().status();
    Notification::create(
        fmt::format("Trim: {} KiB en {:.2f} ms", result.reclaimedKiB, result.durationMs),
        result.beneficial ? NotificationIcon::Success : NotificationIcon::Info
    )->show();
}

void RootConsolePopup::onRestore(CCObject*) {
    SessionManager::get().end();
    Notification::create("Restauración segura solicitada", NotificationIcon::Success)->show();
}

void RootConsolePopup::onReplayProbe(CCObject*) {
    ReplayProbe::get().run();
    Notification::create("Sonda read-only de screenrecord iniciada", NotificationIcon::Info)->show();
}

} // namespace zaid::ultra
