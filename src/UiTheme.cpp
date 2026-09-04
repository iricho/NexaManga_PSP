#include "UiTheme.hpp"

#include <array>

namespace {

constexpr std::array<UiTheme, static_cast<std::size_t>(UiThemeId::Count)> Themes {{
    {UiThemeId::Nexa, "Nexa",
     {12, 15, 20, 255}, {25, 30, 38, 255}, {31, 43, 58, 255},
     {244, 247, 250, 255}, {151, 164, 181, 255}, {35, 151, 255, 255},
     {222, 54, 68, 255}, {58, 70, 86, 255}, {42, 50, 62, 255}, {35, 151, 255, 255}},
    {UiThemeId::MangaMono, "Manga Mono",
     {12, 12, 13, 255}, {29, 29, 31, 255}, {43, 43, 46, 255},
     {247, 247, 247, 255}, {170, 170, 174, 255}, {244, 244, 244, 255},
     {180, 180, 184, 255}, {82, 82, 86, 255}, {53, 53, 56, 255}, {236, 236, 236, 255}},
    {UiThemeId::Crimson, "Crimson",
     {13, 10, 12, 255}, {31, 23, 27, 255}, {54, 27, 34, 255},
     {249, 246, 247, 255}, {184, 159, 166, 255}, {190, 33, 55, 255},
     {245, 245, 245, 255}, {87, 50, 60, 255}, {61, 41, 47, 255}, {205, 38, 61, 255}},
    {UiThemeId::MidnightBlue, "Midnight Blue",
     {8, 13, 24, 255}, {18, 28, 45, 255}, {25, 42, 65, 255},
     {241, 247, 252, 255}, {144, 165, 188, 255}, {33, 190, 235, 255},
     {132, 102, 196, 255}, {47, 68, 94, 255}, {31, 48, 70, 255}, {33, 190, 235, 255}},
    {UiThemeId::Paper, "Paper",
     {239, 233, 218, 255}, {250, 246, 236, 255}, {232, 222, 205, 255},
     {40, 39, 37, 255}, {99, 93, 84, 255}, {166, 61, 57, 255},
     {48, 103, 154, 255}, {174, 163, 144, 255}, {215, 205, 188, 255}, {166, 61, 57, 255}}
}};

} // namespace

bool isValidUiThemeId(UiThemeId id) {
    return static_cast<std::size_t>(id) < Themes.size();
}

const UiTheme& uiTheme(UiThemeId id) {
    return Themes[isValidUiThemeId(id) ? static_cast<std::size_t>(id) : 0u];
}

std::size_t uiThemeCount() {
    return Themes.size();
}

UiThemeId uiThemeIdAt(std::size_t index) {
    return index < Themes.size() ? Themes[index].id : UiThemeId::Nexa;
}

const char* uiThemeKey(UiThemeId id) {
    switch (id) {
        case UiThemeId::Nexa: return "nexa";
        case UiThemeId::MangaMono: return "manga_mono";
        case UiThemeId::Crimson: return "crimson";
        case UiThemeId::MidnightBlue: return "midnight_blue";
        case UiThemeId::Paper: return "paper";
        case UiThemeId::Count: break;
    }
    return "nexa";
}

UiThemeId parseUiThemeId(const std::string& key, bool* valid) {
    UiThemeId result = UiThemeId::Nexa;
    bool matched = true;
    if (key == "nexa") result = UiThemeId::Nexa;
    else if (key == "manga_mono") result = UiThemeId::MangaMono;
    else if (key == "crimson") result = UiThemeId::Crimson;
    else if (key == "midnight_blue") result = UiThemeId::MidnightBlue;
    else if (key == "paper") result = UiThemeId::Paper;
    else matched = false;
    if (valid) *valid = matched;
    return result;
}

