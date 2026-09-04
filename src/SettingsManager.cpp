#include "SettingsManager.hpp"

#include "Persistence.hpp"

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

namespace {

bool parseUnsigned(const std::string& text, std::uint64_t& value) {
    if (text.empty() || text.front() == '-') return false;
    errno = 0;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
    if (errno != 0 || !end || *end != '\0') return false;
    value = static_cast<std::uint64_t>(parsed);
    return true;
}

bool parseFloat(const std::string& text, float& value) {
    errno = 0;
    char* end = nullptr;
    const float parsed = std::strtof(text.c_str(), &end);
    if (errno != 0 || !end || *end != '\0') return false;
    value = parsed;
    return true;
}

} // namespace

SettingsManager::SettingsManager(std::string path)
    : path_(std::move(path)) {
}

bool SettingsManager::load() {
    settings_ = AppSettings {};
    lastError_.clear();

    std::ifstream input(path_, std::ios::binary);
    if (!input) return true;

    std::string line;
    if (!std::getline(input, line)) {
        lastError_ = "Settings file is empty.";
        return false;
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const bool version1 = line == "MANGAPSP_SETTINGS\t1";
    const bool version2 = line == "MANGAPSP_SETTINGS\t2";
    const bool version3 = line == "MANGAPSP_SETTINGS\t3";
    const bool version4 = line == "MANGAPSP_SETTINGS\t4";
    if (!version1 && !version2 && !version3 && !version4) {
        lastError_ = "Unsupported or corrupt settings header.";
        return false;
    }

    std::size_t skipped = 0;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) {
            if (!line.empty()) ++skipped;
            continue;
        }
        const std::string key = line.substr(0, separator);
        const std::string value = line.substr(separator + 1);

        if (key == "direction") {
            if (value == "rtl") settings_.defaultDirection = ReadingDirection::RightToLeft;
            else if (value == "ltr") settings_.defaultDirection = ReadingDirection::LeftToRight;
            else ++skipped;
        } else if (key == "fit") {
            if (value == "page") settings_.defaultFitMode = SavedFitMode::Page;
            else if (value == "width") settings_.defaultFitMode = SavedFitMode::Width;
            else ++skipped;
        } else if (key == "hud_timeout_ms") {
            std::uint64_t parsed = 0;
            if (!parseUnsigned(value, parsed) || parsed < 250u || parsed > 10000u) ++skipped;
            else settings_.hudTimeoutMs = static_cast<unsigned int>(parsed);
        } else if (key == "analog_sensitivity") {
            float parsed = 0.0f;
            if (!parseFloat(value, parsed) || parsed < 0.25f || parsed > 3.0f) ++skipped;
            else settings_.analogSensitivity = parsed;
        } else if (key == "analog_dead_zone") {
            float parsed = 0.0f;
            if (!parseFloat(value, parsed) || parsed < 4.0f || parsed > 48.0f) ++skipped;
            else settings_.analogDeadZone = parsed;
        } else if (key == "image_memory_budget") {
            std::uint64_t parsed = 0;
            if (!parseUnsigned(value, parsed) ||
                parsed > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
                (parsed != 0 && (parsed < 4u * 1024u * 1024u || parsed > 256u * 1024u * 1024u))) {
                ++skipped;
            }
            else settings_.imageMemoryBudgetBytes = static_cast<std::size_t>(parsed);
        } else if (key == "debug_overlay") {
            if (value == "0" || value == "1") settings_.debugOverlay = value == "1";
            else ++skipped;
        } else if (key == "smart_reading") {
            if (value == "0" || value == "1") settings_.smartReading = value == "1";
            else ++skipped;
        } else if (key == "spread_mode") {
            if (value == "auto") settings_.defaultSpreadMode = SpreadMode::Auto;
            else if (value == "full") settings_.defaultSpreadMode = SpreadMode::FullSpread;
            else if (value == "split") settings_.defaultSpreadMode = SpreadMode::SplitSpread;
            else ++skipped;
        } else if (key == "theme") {
            bool valid = false;
            settings_.selectedTheme = parseUiThemeId(value, &valid);
            if (!valid) ++skipped;
        } else if (key == "last_selected_series") {
            if (!Persistence::decodeField(value, settings_.lastSelectedSeriesPath)) ++skipped;
        }
    }

    if (skipped > 0) {
        std::ostringstream warning;
        warning << "Ignored " << skipped << " invalid setting" << (skipped == 1 ? "." : "s.");
        lastError_ = warning.str();
    }
    return true;
}

bool SettingsManager::save() {
    std::ostringstream output;
    const char* spread = settings_.defaultSpreadMode == SpreadMode::FullSpread ? "full" :
                         settings_.defaultSpreadMode == SpreadMode::SplitSpread ? "split" : "auto";
    output << "MANGAPSP_SETTINGS\t4\n"
           << "direction=" << (settings_.defaultDirection == ReadingDirection::LeftToRight ? "ltr" : "rtl") << '\n'
           << "fit=" << (settings_.defaultFitMode == SavedFitMode::Width ? "width" : "page") << '\n'
           << "hud_timeout_ms=" << settings_.hudTimeoutMs << '\n'
           << "analog_sensitivity=" << settings_.analogSensitivity << '\n'
           << "analog_dead_zone=" << settings_.analogDeadZone << '\n'
           << "image_memory_budget=" << settings_.imageMemoryBudgetBytes << '\n'
           << "debug_overlay=" << (settings_.debugOverlay ? 1 : 0) << '\n'
           << "smart_reading=" << (settings_.smartReading ? 1 : 0) << '\n'
           << "spread_mode=" << spread << '\n'
           << "theme=" << uiThemeKey(settings_.selectedTheme) << '\n'
           << "last_selected_series="
           << Persistence::encodeField(settings_.lastSelectedSeriesPath) << '\n';
    lastError_.clear();
    return Persistence::atomicWrite(path_, output.str(), lastError_);
}
