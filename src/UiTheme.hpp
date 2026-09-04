#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

enum class UiThemeId : std::uint8_t {
    Nexa = 0,
    MangaMono,
    Crimson,
    MidnightBlue,
    Paper,
    Count
};

struct UiColor {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

struct UiTheme {
    UiThemeId id = UiThemeId::Nexa;
    const char* name = "Nexa";
    UiColor background;
    UiColor surface;
    UiColor surfaceSelected;
    UiColor textPrimary;
    UiColor textSecondary;
    UiColor accentPrimary;
    UiColor accentSecondary;
    UiColor border;
    UiColor progressBackground;
    UiColor progressFill;
};

const UiTheme& uiTheme(UiThemeId id);
std::size_t uiThemeCount();
UiThemeId uiThemeIdAt(std::size_t index);
const char* uiThemeKey(UiThemeId id);
UiThemeId parseUiThemeId(const std::string& key, bool* valid = nullptr);
bool isValidUiThemeId(UiThemeId id);

