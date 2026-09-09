#include "AndroidBridge.hpp"

#include "../core/Settings.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <vector>

#ifdef GEODE_IS_ANDROID
#include <Geode/cocos/platform/android/jni/JniHelper.h>
#include <jni.h>
#endif

using namespace geode::prelude;

namespace zaid::ultra {
namespace {

#ifdef GEODE_IS_ANDROID

JNIEnv* currentEnv() {
    auto* vm = cocos2d::JniHelper::getJavaVM();
    if (!vm) {
        return nullptr;
    }
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return nullptr;
    }
    return env;
}

bool consumeException(JNIEnv* env, std::string& error, char const* stage) {
    if (!env || !env->ExceptionCheck()) {
        return false;
    }
    env->ExceptionClear();
    error = fmt::format("JNI exception at {}", stage);
    return true;
}

jobject getActivity(JNIEnv* env, std::string& error) {
    cocos2d::JniMethodInfo method;
    if (!cocos2d::JniHelper::getStaticMethodInfo(
        method,
        "org/cocos2dx/lib/Cocos2dxActivity",
        "getContext",
        "()Landroid/content/Context;"
    )) {
        error = "Cocos2dxActivity.getContext unavailable";
        return nullptr;
    }
    auto activity = method.env->CallStaticObjectMethod(method.classID, method.methodID);
    method.env->DeleteLocalRef(method.classID);
    if (consumeException(method.env, error, "getContext") || !activity) {
        if (activity) {
            method.env->DeleteLocalRef(activity);
        }
        return nullptr;
    }
    return activity;
}

jobject getDisplay(JNIEnv* env, jobject activity, std::string& error) {
    auto activityClass = env->GetObjectClass(activity);
    auto getWindowManager = env->GetMethodID(
        activityClass,
        "getWindowManager",
        "()Landroid/view/WindowManager;"
    );
    if (!getWindowManager || consumeException(env, error, "getWindowManager method")) {
        env->DeleteLocalRef(activityClass);
        return nullptr;
    }
    auto windowManager = env->CallObjectMethod(activity, getWindowManager);
    env->DeleteLocalRef(activityClass);
    if (!windowManager || consumeException(env, error, "getWindowManager")) {
        return nullptr;
    }

    auto managerClass = env->GetObjectClass(windowManager);
    auto getDefaultDisplay = env->GetMethodID(
        managerClass,
        "getDefaultDisplay",
        "()Landroid/view/Display;"
    );
    auto display = getDefaultDisplay
        ? env->CallObjectMethod(windowManager, getDefaultDisplay)
        : nullptr;
    env->DeleteLocalRef(managerClass);
    env->DeleteLocalRef(windowManager);
    if (!display || consumeException(env, error, "getDefaultDisplay")) {
        return nullptr;
    }
    return display;
}

float displayRefreshRate(JNIEnv* env, jobject display, std::string& error) {
    auto displayClass = env->GetObjectClass(display);
    auto getRefreshRate = env->GetMethodID(displayClass, "getRefreshRate", "()F");
    auto refresh = getRefreshRate ? env->CallFloatMethod(display, getRefreshRate) : 0.0f;
    env->DeleteLocalRef(displayClass);
    if (!getRefreshRate || consumeException(env, error, "Display.getRefreshRate")) {
        return 0.0f;
    }
    return refresh;
}

int parsePositiveInt(std::string const& value) {
    if (value.empty()) {
        return 0;
    }
    char* end = nullptr;
    auto parsed = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0' || parsed <= 0 || parsed > 1'000'000) {
        return 0;
    }
    return static_cast<int>(parsed);
}

std::string javaString(JNIEnv* env, jstring value) {
    if (!value) {
        return {};
    }
    auto converted = cocos2d::JniHelper::jstring2string(value);
    return std::string(converted.c_str());
}

#endif

} // namespace

AndroidBridge& AndroidBridge::get() {
    static AndroidBridge instance;
    return instance;
}

bool AndroidBridge::requestRefreshRate(float hz) {
#ifdef GEODE_IS_ANDROID
    auto* env = currentEnv();
    std::string error;
    if (!env) {
        std::lock_guard lock(m_mutex);
        m_displayStatus.lastError = "JNIEnv unavailable on current thread";
        return false;
    }

    auto activity = getActivity(env, error);
    if (!activity) {
        std::lock_guard lock(m_mutex);
        m_displayStatus.lastError = error;
        return false;
    }
    auto display = getDisplay(env, activity, error);
    if (!display) {
        env->DeleteLocalRef(activity);
        std::lock_guard lock(m_mutex);
        m_displayStatus.lastError = error;
        return false;
    }

    auto displayClass = env->GetObjectClass(display);
    auto getMode = env->GetMethodID(displayClass, "getMode", "()Landroid/view/Display$Mode;");
    auto getSupportedModes = env->GetMethodID(displayClass, "getSupportedModes", "()[Landroid/view/Display$Mode;");
    auto currentMode = getMode ? env->CallObjectMethod(display, getMode) : nullptr;
    auto modes = getSupportedModes
        ? static_cast<jobjectArray>(env->CallObjectMethod(display, getSupportedModes))
        : nullptr;

    int currentWidth = 0;
    int currentHeight = 0;
    int selectedModeId = 0;
    float closestDistance = 1000.0f;
    std::vector<float> supportedRates;

    if (currentMode && !consumeException(env, error, "Display.getMode")) {
        auto modeClass = env->GetObjectClass(currentMode);
        auto getWidth = env->GetMethodID(modeClass, "getPhysicalWidth", "()I");
        auto getHeight = env->GetMethodID(modeClass, "getPhysicalHeight", "()I");
        if (getWidth && getHeight) {
            currentWidth = env->CallIntMethod(currentMode, getWidth);
            currentHeight = env->CallIntMethod(currentMode, getHeight);
        }
        env->DeleteLocalRef(modeClass);
    }

    if (modes && !consumeException(env, error, "Display.getSupportedModes")) {
        auto count = env->GetArrayLength(modes);
        for (jsize index = 0; index < count; ++index) {
            auto mode = env->GetObjectArrayElement(modes, index);
            if (!mode) {
                continue;
            }
            auto modeClass = env->GetObjectClass(mode);
            auto getId = env->GetMethodID(modeClass, "getModeId", "()I");
            auto getWidth = env->GetMethodID(modeClass, "getPhysicalWidth", "()I");
            auto getHeight = env->GetMethodID(modeClass, "getPhysicalHeight", "()I");
            auto getRate = env->GetMethodID(modeClass, "getRefreshRate", "()F");
            if (getId && getWidth && getHeight && getRate) {
                auto id = env->CallIntMethod(mode, getId);
                auto width = env->CallIntMethod(mode, getWidth);
                auto height = env->CallIntMethod(mode, getHeight);
                auto rate = env->CallFloatMethod(mode, getRate);
                supportedRates.push_back(rate);
                auto distance = std::fabs(rate - hz);
                bool sameResolution = currentWidth == 0 || (width == currentWidth && height == currentHeight);
                if (sameResolution && distance < closestDistance) {
                    closestDistance = distance;
                    selectedModeId = id;
                }
            }
            env->DeleteLocalRef(modeClass);
            env->DeleteLocalRef(mode);
        }
    }

    std::sort(supportedRates.begin(), supportedRates.end());
    supportedRates.erase(
        std::unique(supportedRates.begin(), supportedRates.end(), [](float left, float right) {
            return std::fabs(left - right) < 0.1f;
        }),
        supportedRates.end()
    );
    std::ostringstream supported;
    for (std::size_t index = 0; index < supportedRates.size(); ++index) {
        if (index) {
            supported << '/';
        }
        supported << static_cast<int>(std::lround(supportedRates[index]));
    }

    auto activityClass = env->GetObjectClass(activity);
    auto getWindow = env->GetMethodID(activityClass, "getWindow", "()Landroid/view/Window;");
    auto window = getWindow ? env->CallObjectMethod(activity, getWindow) : nullptr;
    env->DeleteLocalRef(activityClass);

    bool applied = false;
    WindowSnapshot snapshot;
    if (window && !consumeException(env, error, "Activity.getWindow")) {
        auto windowClass = env->GetObjectClass(window);
        auto getAttributes = env->GetMethodID(
            windowClass,
            "getAttributes",
            "()Landroid/view/WindowManager$LayoutParams;"
        );
        auto setAttributes = env->GetMethodID(
            windowClass,
            "setAttributes",
            "(Landroid/view/WindowManager$LayoutParams;)V"
        );
        auto attributes = getAttributes ? env->CallObjectMethod(window, getAttributes) : nullptr;
        if (attributes && setAttributes && !consumeException(env, error, "Window.getAttributes")) {
            auto attributesClass = env->GetObjectClass(attributes);
            auto modeField = env->GetFieldID(attributesClass, "preferredDisplayModeId", "I");
            auto rateField = env->GetFieldID(attributesClass, "preferredRefreshRate", "F");
            if (modeField && rateField && !consumeException(env, error, "preferred display fields")) {
                snapshot.valid = true;
                snapshot.preferredModeId = env->GetIntField(attributes, modeField);
                snapshot.preferredRefreshRate = env->GetFloatField(attributes, rateField);
                if (selectedModeId != 0 && closestDistance <= 0.6f) {
                    env->SetIntField(attributes, modeField, selectedModeId);
                }
                env->SetFloatField(attributes, rateField, hz);
                env->CallVoidMethod(window, setAttributes, attributes);
                applied = !consumeException(env, error, "Window.setAttributes");
            }
            env->DeleteLocalRef(attributesClass);
            env->DeleteLocalRef(attributes);
        }
        env->DeleteLocalRef(windowClass);
        env->DeleteLocalRef(window);
    }

    auto currentRefresh = displayRefreshRate(env, display, error);
    if (modes) env->DeleteLocalRef(modes);
    if (currentMode) env->DeleteLocalRef(currentMode);
    env->DeleteLocalRef(displayClass);
    env->DeleteLocalRef(display);
    env->DeleteLocalRef(activity);

    {
        std::lock_guard lock(m_mutex);
        if (applied) {
            m_windowSnapshot = snapshot;
        }
        m_displayStatus.currentRefreshHz = currentRefresh;
        m_displayStatus.requestedRefreshHz = applied ? hz : 0.0f;
        m_displayStatus.selectedModeId = applied ? selectedModeId : 0;
        m_displayStatus.requestActive = applied;
        m_displayStatus.supportedModes = supported.str().empty() ? "<no disponible>" : supported.str();
        m_displayStatus.lastError = applied ? std::string{} : error;
    }

    if (settings::diagnostics()) {
        if (applied) {
            log::info(
                "Per-window refresh request: {:.1f} Hz, selected mode={}, supported={} Hz",
                hz,
                selectedModeId,
                supported.str()
            );
        } else {
            log::warn("Per-window refresh request failed: {}", error);
        }
    }
    return applied;
#else
    (void)hz;
    return false;
#endif
}

void AndroidBridge::restoreRefreshRate() {
#ifdef GEODE_IS_ANDROID
    WindowSnapshot snapshot;
    {
        std::lock_guard lock(m_mutex);
        snapshot = m_windowSnapshot;
    }
    if (!snapshot.valid) {
        return;
    }

    auto* env = currentEnv();
    std::string error;
    auto activity = env ? getActivity(env, error) : nullptr;
    if (!env || !activity) {
        std::lock_guard lock(m_mutex);
        m_displayStatus.lastError = error.empty() ? "JNIEnv unavailable during restore" : error;
        return;
    }

    auto activityClass = env->GetObjectClass(activity);
    auto getWindow = env->GetMethodID(activityClass, "getWindow", "()Landroid/view/Window;");
    auto window = getWindow ? env->CallObjectMethod(activity, getWindow) : nullptr;
    env->DeleteLocalRef(activityClass);
    bool restored = false;

    if (window && !consumeException(env, error, "restore getWindow")) {
        auto windowClass = env->GetObjectClass(window);
        auto getAttributes = env->GetMethodID(windowClass, "getAttributes", "()Landroid/view/WindowManager$LayoutParams;");
        auto setAttributes = env->GetMethodID(windowClass, "setAttributes", "(Landroid/view/WindowManager$LayoutParams;)V");
        auto attributes = getAttributes ? env->CallObjectMethod(window, getAttributes) : nullptr;
        if (attributes && setAttributes && !consumeException(env, error, "restore getAttributes")) {
            auto attributesClass = env->GetObjectClass(attributes);
            auto modeField = env->GetFieldID(attributesClass, "preferredDisplayModeId", "I");
            auto rateField = env->GetFieldID(attributesClass, "preferredRefreshRate", "F");
            if (modeField && rateField && !consumeException(env, error, "restore preferred fields")) {
                env->SetIntField(attributes, modeField, snapshot.preferredModeId);
                env->SetFloatField(attributes, rateField, snapshot.preferredRefreshRate);
                env->CallVoidMethod(window, setAttributes, attributes);
                restored = !consumeException(env, error, "restore Window.setAttributes");
            }
            env->DeleteLocalRef(attributesClass);
            env->DeleteLocalRef(attributes);
        }
        env->DeleteLocalRef(windowClass);
        env->DeleteLocalRef(window);
    }
    env->DeleteLocalRef(activity);

    {
        std::lock_guard lock(m_mutex);
        if (restored) {
            m_windowSnapshot = {};
            m_displayStatus.requestActive = false;
            m_displayStatus.requestedRefreshHz = 0.0f;
            m_displayStatus.selectedModeId = 0;
            m_displayStatus.lastError.clear();
        } else {
            m_displayStatus.lastError = error;
        }
    }
    if (settings::diagnostics()) {
        if (restored) {
            log::info("Per-window refresh preference restored exactly");
        } else {
            log::warn("Per-window refresh restore failed: {}", error);
        }
    }
#endif
}

float AndroidBridge::sampleRefreshRate() {
#ifdef GEODE_IS_ANDROID
    auto* env = currentEnv();
    std::string error;
    auto activity = env ? getActivity(env, error) : nullptr;
    auto display = activity ? getDisplay(env, activity, error) : nullptr;
    auto value = display ? displayRefreshRate(env, display, error) : 0.0f;
    if (display) env->DeleteLocalRef(display);
    if (activity) env->DeleteLocalRef(activity);
    {
        std::lock_guard lock(m_mutex);
        if (value > 0.0f) {
            m_displayStatus.currentRefreshHz = value;
        }
        if (!error.empty()) {
            m_displayStatus.lastError = error;
        }
    }
    return value;
#else
    return 0.0f;
#endif
}

DisplayStatus AndroidBridge::displayStatus() const {
    std::lock_guard lock(m_mutex);
    return m_displayStatus;
}

AndroidAudioProperties AndroidBridge::queryAudioProperties() {
#ifdef GEODE_IS_ANDROID
    AndroidAudioProperties properties;
    auto* env = currentEnv();
    std::string error;
    auto activity = env ? getActivity(env, error) : nullptr;
    if (!env || !activity) {
        properties.lastError = error.empty() ? "JNIEnv unavailable" : error;
        std::lock_guard lock(m_mutex);
        m_audioProperties = properties;
        return properties;
    }

    auto contextClass = env->GetObjectClass(activity);
    auto getSystemService = env->GetMethodID(
        contextClass,
        "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;"
    );
    auto audioName = env->NewStringUTF("audio");
    auto audioManager = getSystemService
        ? env->CallObjectMethod(activity, getSystemService, audioName)
        : nullptr;
    env->DeleteLocalRef(audioName);
    env->DeleteLocalRef(contextClass);

    if (audioManager && !consumeException(env, error, "AudioManager service")) {
        auto audioClass = env->GetObjectClass(audioManager);
        auto getProperty = env->GetMethodID(
            audioClass,
            "getProperty",
            "(Ljava/lang/String;)Ljava/lang/String;"
        );
        auto readProperty = [&](char const* key) {
            auto javaKey = env->NewStringUTF(key);
            auto value = getProperty
                ? static_cast<jstring>(env->CallObjectMethod(audioManager, getProperty, javaKey))
                : nullptr;
            env->DeleteLocalRef(javaKey);
            auto converted = javaString(env, value);
            if (value) env->DeleteLocalRef(value);
            return converted;
        };
        properties.sampleRate = parsePositiveInt(readProperty("android.media.property.OUTPUT_SAMPLE_RATE"));
        properties.framesPerBuffer = parsePositiveInt(readProperty("android.media.property.OUTPUT_FRAMES_PER_BUFFER"));
        env->DeleteLocalRef(audioClass);
        env->DeleteLocalRef(audioManager);
    }

    auto activityClass = env->GetObjectClass(activity);
    auto getPackageManager = env->GetMethodID(
        activityClass,
        "getPackageManager",
        "()Landroid/content/pm/PackageManager;"
    );
    auto packageManager = getPackageManager
        ? env->CallObjectMethod(activity, getPackageManager)
        : nullptr;
    env->DeleteLocalRef(activityClass);
    if (packageManager && !consumeException(env, error, "PackageManager")) {
        auto packageClass = env->GetObjectClass(packageManager);
        auto hasFeature = env->GetMethodID(
            packageClass,
            "hasSystemFeature",
            "(Ljava/lang/String;)Z"
        );
        auto featureName = env->NewStringUTF("android.hardware.audio.low_latency");
        auto supported = hasFeature
            ? env->CallBooleanMethod(packageManager, hasFeature, featureName)
            : JNI_FALSE;
        properties.lowLatencyFeature = supported == JNI_TRUE ? "sí" : "no";
        env->DeleteLocalRef(featureName);
        env->DeleteLocalRef(packageClass);
        env->DeleteLocalRef(packageManager);
    }
    env->DeleteLocalRef(activity);
    if (consumeException(env, error, "audio properties")) {
        properties.lastError = error;
    }

    {
        std::lock_guard lock(m_mutex);
        m_audioProperties = properties;
    }
    return properties;
#else
    return {};
#endif
}

AndroidAudioProperties AndroidBridge::audioProperties() const {
    std::lock_guard lock(m_mutex);
    return m_audioProperties;
}

} // namespace zaid::ultra
