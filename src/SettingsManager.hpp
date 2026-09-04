#pragma once

#include "SaveManager.hpp"
#include "UiTheme.hpp"

#include <cstddef>
#include <string>

struct AppSettings {
    ReadingDirection defaultDirection = ReadingDirection::RightToLeft;
    SavedFitMode defaultFitMode = SavedFitMode::Page;
    unsigned int hudTimeoutMs = 2500;
    float analogSensitivity = 1.0f;
    float analogDeadZone = 18.0f;
    std::size_t imageMemoryBudgetBytes = 0;
    bool debugOverlay = false;
    bool smartReading = true;
    SpreadMode defaultSpreadMode = SpreadMode::Auto;
    UiThemeId selectedTheme = UiThemeId::Nexa;
    std::string lastSelectedSeriesPath;
};

class SettingsManager {
public:
    explicit SettingsManager(std::string path);

    bool load();
    bool save();

    AppSettings& settings() { return settings_; }
    const AppSettings& settings() const { return settings_; }
    const std::string& lastError() const { return lastError_; }

private:
    std::string path_;
    AppSettings settings_;
    std::string lastError_;
};
