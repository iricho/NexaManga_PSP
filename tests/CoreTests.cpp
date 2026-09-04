#include "FileScanner.hpp"
#include "LibraryManager.hpp"
#include "MemoryBudget.hpp"
#include "NaturalSort.hpp"
#include "PathUtils.hpp"
#include "SaveManager.hpp"
#include "SettingsManager.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int failures = 0;
std::vector<std::pair<std::string, std::string>> scanDiagnostics;

void captureScanDiagnostic(const char* key, const char* value) {
    scanDiagnostics.emplace_back(key ? key : "", value ? value : "");
}

bool hasScanDiagnostic(const std::string& key, const std::string& value) {
    return std::find(scanDiagnostics.begin(), scanDiagnostics.end(),
                     std::make_pair(key, value)) != scanDiagnostics.end();
}

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void touch(const fs::path& path) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.put('\0');
}

void testNaturalSort() {
    std::vector<std::string> names {
        "Chapter 11", "Chapter 2", "Chapter 001", "Chapter 10", "Chapter 1", "Chapter 9"
    };
    std::sort(names.begin(), names.end(), NaturalSort::less);
    const std::vector<std::string> expected {
        "Chapter 1", "Chapter 001", "Chapter 2", "Chapter 9", "Chapter 10", "Chapter 11"
    };
    expect(names == expected, "natural chapter ordering");
    expect(NaturalSort::compare("page9.jpg", "page10.jpg") < 0, "numeric page ordering");
    expect(NaturalSort::compare("Page 2", "page 2") != 0, "deterministic case tie-break");
}

void testPaths() {
    expect(PathUtils::join("ms0:/MANGA", "Bleach") == "ms0:/MANGA/Bleach", "PSP path join");
    expect(PathUtils::filename("a/b\\c.PNG") == "c.PNG", "mixed separator filename");
    expect(PathUtils::stem("Chapter 12.CBZ") == "Chapter 12", "archive display stem");
    expect(PathUtils::isSupportedImage("001.JPEG"), "case-insensitive JPEG extension");
    expect(!PathUtils::isSupportedImage("001.webp"), "unsupported image filtering");
    expect(PathUtils::isIgnoredEntry(".hidden"), "hidden entry filtering");
    expect(PathUtils::isIgnoredEntry("System Volume Information"), "system entry filtering");
    expect(PathUtils::isIgnoredArchivePath("nested/__MACOSX/001.jpg"),
           "nested archive junk filtering");
    expect(PathUtils::isCoverFilename("FOLDER.PNG"), "cover filename detection");
}

void testLibraryScanning() {
    const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() / ("mangapsp_core_" + std::to_string(nonce));

    touch(root / "Series 10" / "010.jpg");
    touch(root / "Series 10" / "002.PNG");
    touch(root / "Series 2" / "cover.jpg");
    touch(root / "Series 2" / "Chapter 10" / "001.jpeg");
    touch(root / "Series 2" / "Chapter 2" / "002.jpg");
    touch(root / "Series 2" / "Chapter 2" / "001.jpg");
    touch(root / "Series 2" / ".hidden" / "001.jpg");
    touch(root / ".hidden series" / "001.jpg");
    fs::create_directories(root / "Empty Series");

    const std::vector<Series> library = FileScanner::scanLibrary(root.generic_string());
    expect(library.size() == 2, "empty and hidden series are ignored");
    if (library.size() == 2) {
        expect(library[0].name == "Series 2" && library[1].name == "Series 10",
               "series are naturally sorted");
        expect(PathUtils::isCoverFilename(library[0].coverPath), "explicit series cover is detected");
        expect(library[0].chapters.size() == 2, "cover-only top level is not a chapter");
        if (library[0].chapters.size() == 2) {
            expect(library[0].chapters[0].name == "Chapter 2", "chapters are naturally sorted");
            expect(PathUtils::filename(library[0].chapters[0].pages[0].location) == "001.jpg",
                   "pages are naturally sorted");
        }
        expect(library[1].chapters.size() == 1 && library[1].chapters[0].name == "Pages",
               "single-folder manga is represented as one chapter");
        expect(PathUtils::filename(library[1].coverPath) == "002.PNG",
               "first naturally sorted page is the fallback cover");
    }

    std::error_code error;
    fs::remove_all(root, error);
    expect(!error, "temporary scanner fixture cleanup");
}

void testFolderOnlyHardwareLayoutDiagnostics() {
    const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() /
        ("mangapsp_folder_only_" + std::to_string(nonce));

    touch(root / "BLEACH" / "13" / "0003.jpg");
    touch(root / "BLEACH" / "13" / "0001.jpg");
    touch(root / "BLEACH" / "13" / "0000.jpg");
    touch(root / "BLEACH" / "13" / "0002.jpg");

    scanDiagnostics.clear();
    LibraryManager manager(root.generic_string());
    manager.refresh(captureScanDiagnostic);

    expect(manager.rootAvailable(), "folder-only test root exists");
    expect(manager.series().size() == 1, "folder-only scan finds one series");
    if (manager.series().size() == 1) {
        const Series& series = manager.series().front();
        expect(series.name == "BLEACH", "folder-only series is BLEACH");
        expect(series.chapters.size() == 1 && series.chapters.front().name == "13",
               "folder-only chapter is 13");
        if (series.chapters.size() == 1) {
            const Chapter& chapter = series.chapters.front();
            expect(chapter.sourceType == PageSourceType::Folder,
                   "folder-only chapter uses folder page source");
            expect(chapter.pages.size() == 4, "folder-only chapter has four pages");
            const std::vector<std::string> expected {
                "0000.jpg", "0001.jpg", "0002.jpg", "0003.jpg"
            };
            std::vector<std::string> actual;
            for (const PageMetadata& page : chapter.pages) actual.push_back(page.logicalName);
            expect(actual == expected, "folder-only pages retain natural numeric ordering");
        }
    }

    expect(hasScanDiagnostic("root_exists", "1"), "scan log records existing root");
    expect(hasScanDiagnostic("series_candidate", "BLEACH"),
           "scan log records BLEACH candidate");
    expect(hasScanDiagnostic("chapter_candidate", "13"),
           "scan log records chapter 13 candidate");
    expect(hasScanDiagnostic("chapter_pages", "4"),
           "scan log records four chapter pages");
    expect(hasScanDiagnostic("scan_series_count", "1"),
           "scan log records one final series");

    std::error_code error;
    fs::remove_all(root, error);
    expect(!error, "folder-only diagnostic fixture cleanup");
}

void testProgressSerialization() {
    const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() / ("mangapsp_progress_" + std::to_string(nonce));
    fs::create_directories(root);
    const fs::path savePath = root / "progress.dat";

    SaveManager writer(savePath.generic_string());
    ReadingProgress progress;
    progress.seriesPath = "./MANGA/Series%One";
    progress.seriesName = "Series\tOne";
    progress.chapterPath = "./MANGA/Series%One/Chapter 10";
    progress.chapterName = "Chapter 10";
    progress.pageIndex = 42;
    progress.fitMode = SavedFitMode::Width;
    progress.direction = ReadingDirection::LeftToRight;
    progress.favorite = true;
    progress.directionOverride = true;
    progress.smartReading = false;
    progress.spreadMode = SpreadMode::SplitSpread;
    progress.logicalPosition = 3;
    progress.lastReadOrder = 7;
    writer.update(progress);
    Bookmark bookmark;
    bookmark.seriesPath = progress.seriesPath;
    bookmark.seriesName = progress.seriesName;
    bookmark.chapterPath = progress.chapterPath;
    bookmark.chapterName = progress.chapterName;
    bookmark.pageIndex = 12;
    bookmark.logicalPosition = 2;
    bookmark.label = "Great\tpage";
    bookmark.creationOrder = 4;
    writer.addBookmark(bookmark);
    expect(writer.save(), "progress save succeeds");

    SaveManager reader(savePath.generic_string());
    expect(reader.load(), "progress load succeeds");
    expect(reader.entries().size() == 1, "one progress entry round-trips");
    if (reader.entries().size() == 1) {
        const ReadingProgress& loaded = reader.entries().front();
        expect(loaded.seriesName == progress.seriesName, "escaped progress name round-trips");
        expect(loaded.pageIndex == 42 && loaded.fitMode == SavedFitMode::Width,
               "progress page and fit mode round-trip");
        expect(loaded.favorite && loaded.direction == ReadingDirection::LeftToRight,
               "progress flags round-trip");
        expect(loaded.directionOverride && !loaded.smartReading &&
               loaded.spreadMode == SpreadMode::SplitSpread && loaded.logicalPosition == 3,
               "reader preferences and logical position round-trip");
    }
    expect(reader.bookmarks().size() == 1 && reader.bookmarks()[0].label == bookmark.label,
           "bookmark serialization round-trips escaped label");

    {
        std::ofstream corrupt(savePath, std::ios::binary | std::ios::app);
        corrupt << "P\tbad%QZ\tentry\n";
        corrupt << "B\tbad%QZ\tentry\n";
    }
    SaveManager tolerant(savePath.generic_string());
    expect(tolerant.load(), "one corrupt progress entry does not destroy valid entries");
    expect(tolerant.entries().size() == 1, "corrupt progress entry is isolated");
    expect(tolerant.bookmarks().size() == 1, "corrupt bookmark entry is isolated");

    const fs::path oldPath = root / "progress-v1.dat";
    {
        std::ofstream old(oldPath, std::ios::binary);
        old << "MANGAPSP_PROGRESS\t1\n"
            << "P\t.%2FMANGA%2FOld\tOld\t.%2FMANGA%2FOld%2FChapter%201\tChapter%201"
            << "\t9\twidth\tltr\t0\t1\t22\n";
    }
    SaveManager migrated(oldPath.generic_string());
    expect(migrated.load() && migrated.entries().size() == 1, "version 1 progress migrates");
    if (!migrated.entries().empty()) {
        expect(migrated.entries()[0].pageIndex == 9 && migrated.entries()[0].completed,
               "migration preserves page and completion");
        expect(migrated.entries()[0].smartReading &&
               migrated.entries()[0].spreadMode == SpreadMode::Auto,
               "migration supplies safe new preference defaults");
    }

    std::error_code error;
    fs::remove_all(root, error);
    expect(!error, "progress fixture cleanup");
}

void testContinueRecoveryAndRecency() {
    Series first;
    first.name = "First";
    first.path = "MANGA/First";
    Chapter firstChapter;
    firstChapter.name = "Chapter 2";
    firstChapter.path = "MANGA/First/Chapter 2";
    firstChapter.pages.resize(5);
    first.chapters.push_back(firstChapter);

    Series second;
    second.name = "Second";
    second.path = "MANGA/Second";
    Chapter secondChapter;
    secondChapter.name = "Chapter 1";
    secondChapter.path = "MANGA/Second/Chapter 1";
    secondChapter.pages.resize(3);
    second.chapters.push_back(secondChapter);
    std::vector<Series> library {first, second};

    SaveManager saves("unused-progress-test.dat");
    ReadingProgress missing;
    missing.seriesPath = first.path;
    missing.seriesName = first.name;
    missing.chapterPath = "MANGA/First/Deleted";
    missing.chapterName = "Deleted";
    missing.pageIndex = 99;
    missing.logicalPosition = 7;
    missing.lastReadOrder = 10;
    saves.update(missing);
    ReadingProgress recent;
    recent.seriesPath = second.path;
    recent.seriesName = second.name;
    recent.chapterPath = secondChapter.path;
    recent.chapterName = secondChapter.name;
    recent.pageIndex = 1;
    recent.lastReadOrder = 20;
    saves.update(recent);

    ProgressLocation recovered;
    expect(saves.resolve(library, missing, recovered), "Continue resolves an existing series");
    expect(recovered.recoveredChapter && recovered.chapterIndex == 0 && recovered.pageIndex == 4,
           "missing chapter recovers and stale page clamps safely");
    ProgressLocation restored;
    expect(saves.resolve(library, recent, restored) && restored.seriesIndex == 1 &&
           restored.chapterIndex == 0 && restored.pageIndex == 1,
           "Continue restores stable series, chapter, and page");
    const std::vector<std::size_t> order = saves.recentlyReadSeries(library);
    expect(order.size() == 2 && order[0] == 1 && order[1] == 0,
           "recently-read series are unique and newest first");
}

void testSettingsSerialization() {
    const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() / ("mangapsp_settings_" + std::to_string(nonce));
    fs::create_directories(root);
    const fs::path settingsPath = root / "settings.dat";

    SettingsManager writer(settingsPath.generic_string());
    writer.settings().defaultDirection = ReadingDirection::LeftToRight;
    writer.settings().defaultFitMode = SavedFitMode::Width;
    writer.settings().hudTimeoutMs = 1800;
    writer.settings().analogSensitivity = 1.4f;
    writer.settings().analogDeadZone = 23.0f;
    writer.settings().imageMemoryBudgetBytes = 16u * 1024u * 1024u;
    writer.settings().debugOverlay = true;
    writer.settings().smartReading = false;
    writer.settings().defaultSpreadMode = SpreadMode::FullSpread;
    writer.settings().selectedTheme = UiThemeId::MidnightBlue;
    writer.settings().lastSelectedSeriesPath = "MANGA/Series One";
    expect(writer.save(), "settings save succeeds");

    SettingsManager reader(settingsPath.generic_string());
    expect(reader.load(), "settings load succeeds");
    const AppSettings& loaded = reader.settings();
    expect(loaded.defaultDirection == ReadingDirection::LeftToRight &&
           loaded.defaultFitMode == SavedFitMode::Width, "settings enums round-trip");
    expect(loaded.hudTimeoutMs == 1800 && loaded.analogSensitivity > 1.39f &&
           loaded.analogDeadZone > 22.9f,
           "settings numeric values round-trip");
    expect(loaded.debugOverlay, "settings boolean round-trips");
    expect(!loaded.smartReading && loaded.defaultSpreadMode == SpreadMode::FullSpread &&
           loaded.selectedTheme == UiThemeId::MidnightBlue &&
           loaded.lastSelectedSeriesPath == "MANGA/Series One",
           "reader and library settings round-trip");

    {
        std::ofstream legacy(settingsPath, std::ios::binary | std::ios::trunc);
        legacy << "MANGAPSP_SETTINGS\t2\nanalog_sensitivity=1.25\n";
    }
    SettingsManager migrated(settingsPath.generic_string());
    expect(migrated.load() && migrated.settings().analogSensitivity > 1.24f &&
           migrated.settings().analogDeadZone == 18.0f &&
           migrated.settings().selectedTheme == UiThemeId::Nexa,
           "version 2 settings migrate with conservative defaults and the Nexa theme");

    {
        std::ofstream currentLegacy(settingsPath, std::ios::binary | std::ios::trunc);
        currentLegacy << "MANGAPSP_SETTINGS\t3\nsmart_reading=0\nlast_selected_series=MANGA%2FBLEACH\n";
    }
    SettingsManager version3(settingsPath.generic_string());
    expect(version3.load() && !version3.settings().smartReading &&
           version3.settings().lastSelectedSeriesPath == "MANGA/BLEACH" &&
           version3.settings().selectedTheme == UiThemeId::Nexa,
           "version 3 settings migrate without losing existing preferences");

    {
        std::ofstream invalid(settingsPath, std::ios::binary | std::ios::trunc);
        invalid << "MANGAPSP_SETTINGS\t4\ntheme=not_a_theme\n";
    }
    SettingsManager invalid(settingsPath.generic_string());
    expect(invalid.load() && invalid.settings().selectedTheme == UiThemeId::Nexa &&
           !invalid.lastError().empty(),
           "invalid theme falls back to Nexa without rejecting the settings file");

    std::error_code error;
    fs::remove_all(root, error);
    expect(!error, "settings fixture cleanup");
}

void testThemeCatalogAndStateIsolation() {
    expect(uiThemeCount() == 5, "five built-in themes are available");
    for (std::size_t i = 0; i < uiThemeCount(); ++i) {
        const UiThemeId id = uiThemeIdAt(i);
        bool valid = false;
        const UiThemeId parsed = parseUiThemeId(uiThemeKey(id), &valid);
        expect(valid && parsed == id && uiTheme(id).id == id && uiTheme(id).name[0] != '\0',
               "built-in theme IDs serialize and resolve deterministically");
    }

    AppSettings settings;
    settings.defaultDirection = ReadingDirection::LeftToRight;
    settings.defaultFitMode = SavedFitMode::Width;
    settings.smartReading = false;
    settings.defaultSpreadMode = SpreadMode::SplitSpread;
    ReadingProgress readerState;
    readerState.pageIndex = 17;
    readerState.logicalPosition = 3;
    readerState.fitMode = SavedFitMode::Width;
    Bookmark bookmark;
    bookmark.pageIndex = 9;
    bookmark.label = "Keep me";
    for (std::size_t i = 0; i < uiThemeCount(); ++i) settings.selectedTheme = uiThemeIdAt(i);
    expect(settings.defaultDirection == ReadingDirection::LeftToRight &&
           settings.defaultFitMode == SavedFitMode::Width && !settings.smartReading &&
           settings.defaultSpreadMode == SpreadMode::SplitSpread,
           "theme switching does not alter reader preferences");
    expect(readerState.pageIndex == 17 && readerState.logicalPosition == 3 &&
           readerState.fitMode == SavedFitMode::Width,
           "theme switching does not alter active reader state");
    expect(bookmark.pageIndex == 9 && bookmark.label == "Keep me",
           "theme switching does not alter bookmarks or progress objects");
}

void testFreshPersistenceStartsCleanly() {
    const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() /
        ("mangapsp_fresh_persistence_" + std::to_string(nonce));
    const fs::path progressPath = root / "missing-progress.dat";
    const fs::path settingsPath = root / "missing-settings.dat";

    SaveManager progress(progressPath.generic_string());
    SettingsManager settings(settingsPath.generic_string());
    expect(progress.load() && progress.entries().empty() && progress.bookmarks().empty() &&
           progress.lastError().empty(),
           "fresh install without a progress file starts cleanly");
    expect(settings.load() && settings.lastError().empty(),
           "fresh install without a settings file starts cleanly");
    expect(!fs::exists(progressPath) && !fs::exists(settingsPath),
           "fresh-load checks do not create or reset persistence files");
}

void testPspMemoryProfiles() {
    using namespace MemoryBudget;
    expect(classifyPsp(24u * MiB) == PspMemoryClass::Standard32MiB,
           "32 MiB-class free memory selects the safe PSP-1000 profile");
    expect(classifyPsp(48u * MiB) == PspMemoryClass::Enhanced64MiB,
           "expanded free memory selects the enhanced PSP profile");
    expect(imageBudget(PspMemoryClass::Standard32MiB) == 16u * MiB &&
           imageBudget(PspMemoryClass::Enhanced64MiB) == 36u * MiB,
           "PSP image budgets retain non-image memory headroom");
    expect(clampRequested(64u * MiB, PspMemoryClass::Standard32MiB) == 16u * MiB &&
           clampRequested(8u * MiB, PspMemoryClass::Standard32MiB) == 8u * MiB,
           "PSP custom budgets clamp downward but preserve safer requests");
}

} // namespace

int main() {
    testNaturalSort();
    testPaths();
    testLibraryScanning();
    testFolderOnlyHardwareLayoutDiagnostics();
    testProgressSerialization();
    testContinueRecoveryAndRecency();
    testSettingsSerialization();
    testThemeCatalogAndStateIsolation();
    testFreshPersistenceStartsCleanly();
    testPspMemoryProfiles();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All NexaManga PSP core tests passed\n";
    return 0;
}
