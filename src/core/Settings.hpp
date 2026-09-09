#pragma once

#include <Geode/Geode.hpp>

#include <string>

namespace zaid::ultra::settings {

inline bool enabled(char const* key) {
    return geode::Mod::get()->getSettingValue<bool>(key);
}

inline std::string text(char const* key) {
    return geode::Mod::get()->getSettingValue<std::string>(key);
}

inline bool diagnostics() {
    return enabled("diagnostic-logs");
}

} // namespace zaid::ultra::settings
