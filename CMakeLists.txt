cmake_minimum_required(VERSION 3.16)

project(MangaPSP VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MANGAPSP_BUILD_APP "Build the SDL2 reader application" ON)
option(MANGAPSP_BUILD_PSP_SMOKE "Build the minimal PSP SDL smoke executable" OFF)
option(MANGAPSP_FETCH_MINIZIP "Fetch a pinned minizip-ng for desktop development" OFF)
option(MANGAPSP_FETCH_JPEG "Fetch a pinned libjpeg-turbo for desktop development" OFF)
option(MANGAPSP_BUILD_BENCHMARKS "Build development-only decoder benchmarks" OFF)
option(MANGAPSP_PSP_RC
    "Build the optimized PSP release candidate with lightweight observability" OFF)
option(MANGAPSP_BOOT_DIAGNOSTICS "Enable persistent PSP boot tracing" OFF)
set(MANGAPSP_BOOT_HOLD_STAGE "0" CACHE STRING
    "Development-only PSP boot stage to hold at (0 disables holds)")
set(MANGAPSP_BOOT_HOLD_CHECKPOINT "0" CACHE STRING
    "Development-only fine boot checkpoint hold: 0 or 31 through 37")
option(MANGAPSP_DEFER_PSP_INPUT_SETUP
    "Development-only: configure PSP controller after SDL_Init" OFF)
option(MANGAPSP_DISABLE_CBZ "Development-only: skip CBZ discovery and use" OFF)
option(MANGAPSP_DISABLE_THUMBNAILS "Development-only: disable thumbnail cache creation/use" OFF)
option(MANGAPSP_IGNORE_PERSISTENCE "Development-only: do not load or write settings/progress" OFF)
option(MANGAPSP_FORCE_NO_MANGA "Development-only: skip all manga root discovery/scanning" OFF)
option(MANGAPSP_HOLD_AFTER_FOLDER_SCAN
    "Development-only PSP hold after folder scan, before reader/thumbnail initialization" OFF)
set(MANGAPSP_PSP_MEMORY_PROFILE "AUTO" CACHE STRING
    "PSP memory profile: AUTO, PSP_1000, or PSP_2000_PLUS")
set_property(CACHE MANGAPSP_PSP_MEMORY_PROFILE PROPERTY STRINGS AUTO PSP_1000 PSP_2000_PLUS)

if(PSP AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MANGAPSP_PSP_RC)
    if(NOT PSP)
        message(FATAL_ERROR "MANGAPSP_PSP_RC is only valid with the PSP toolchain")
    endif()
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(FATAL_ERROR "MANGAPSP_PSP_RC must use an optimized non-Debug build type")
    endif()

    # RC is one normal-feature build profile, not another isolation matrix.
    set(MANGAPSP_BUILD_APP ON CACHE BOOL "" FORCE)
    set(MANGAPSP_BUILD_PSP_SMOKE OFF CACHE BOOL "" FORCE)
    set(MANGAPSP_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
    set(MANGAPSP_FETCH_MINIZIP OFF CACHE BOOL "" FORCE)
    set(MANGAPSP_BOOT_DIAGNOSTICS ON CACHE BOOL "" FORCE)
    set(MANGAPSP_BOOT_HOLD_STAGE "0" CACHE STRING "" FORCE)
    set(MANGAPSP_BOOT_HOLD_CHECKPOINT "0" CACHE STRING "" FORCE)
    set(MANGAPSP_DEFER_PSP_INPUT_SETUP OFF CACHE BOOL "" FORCE)
    set(MANGAPSP_DISABLE_CBZ OFF CACHE BOOL "" FORCE)
    set(MANGAPSP_DISABLE_THUMBNAILS OFF CACHE BOOL "" FORCE)
    set(MANGAPSP_IGNORE_PERSISTENCE OFF CACHE BOOL "" FORCE)
    set(MANGAPSP_FORCE_NO_MANGA OFF CACHE BOOL "" FORCE)
    set(MANGAPSP_HOLD_AFTER_FOLDER_SCAN OFF CACHE BOOL "" FORCE)
endif()

if(NOT MANGAPSP_PSP_MEMORY_PROFILE MATCHES "^(AUTO|PSP_1000|PSP_2000_PLUS)$")
    message(FATAL_ERROR "MANGAPSP_PSP_MEMORY_PROFILE must be AUTO, PSP_1000, or PSP_2000_PLUS")
endif()

if(NOT MANGAPSP_BOOT_HOLD_STAGE MATCHES "^([0-9]|1[0-6])$")
    message(FATAL_ERROR "MANGAPSP_BOOT_HOLD_STAGE must be an integer from 0 through 16")
endif()

if(NOT MANGAPSP_BOOT_HOLD_CHECKPOINT MATCHES "^(0|3[1-7])$")
    message(FATAL_ERROR "MANGAPSP_BOOT_HOLD_CHECKPOINT must be 0 or 31 through 37")
endif()

if(PSP AND MANGAPSP_BUILD_APP AND MANGAPSP_BUILD_PSP_SMOKE)
    message(FATAL_ERROR
        "Configure the PSP smoke target separately with MANGAPSP_BUILD_APP=OFF; "
        "both targets emit EBOOT.PBP")
endif()

set(MANGAPSP_COMPILE_CBZ ON)
if(MANGAPSP_DISABLE_CBZ AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(MANGAPSP_COMPILE_CBZ OFF)
endif()

if((MANGAPSP_COMPILE_CBZ AND MANGAPSP_FETCH_MINIZIP) OR MANGAPSP_FETCH_JPEG)
    include(FetchContent)
endif()

if(NOT MANGAPSP_COMPILE_CBZ)
    message(STATUS "CBZ/minizip compiled out for this folder-only Debug build")
elseif(MANGAPSP_FETCH_MINIZIP)
    set(MZ_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(MZ_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    set(MZ_BUILD_FUZZ_TESTS OFF CACHE BOOL "" FORCE)
    set(MZ_COMPAT OFF CACHE BOOL "" FORCE)
    set(MZ_BZIP2 OFF CACHE BOOL "" FORCE)
    set(MZ_LZMA OFF CACHE BOOL "" FORCE)
    set(MZ_PPMD OFF CACHE BOOL "" FORCE)
    set(MZ_ZSTD OFF CACHE BOOL "" FORCE)
    set(MZ_PKCRYPT OFF CACHE BOOL "" FORCE)
    set(MZ_WZAES OFF CACHE BOOL "" FORCE)
    set(MZ_OPENSSL OFF CACHE BOOL "" FORCE)
    set(MZ_LIBBSD OFF CACHE BOOL "" FORCE)
    set(MZ_ICONV OFF CACHE BOOL "" FORCE)
    set(MZ_SIGNING OFF CACHE BOOL "" FORCE)
    set(MZ_FETCH_LIBS ON CACHE BOOL "" FORCE)
    set(MZ_DECOMPRESS_ONLY ON CACHE BOOL "" FORCE)
    set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(zlib
        URL https://github.com/madler/zlib/archive/refs/tags/v1.3.1.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_Declare(minizip_ng
        URL https://github.com/zlib-ng/minizip-ng/archive/refs/tags/3.0.4.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(minizip_ng)
    if(TARGET MINIZIP::minizip)
        set(MANGAPSP_MINIZIP_TARGET MINIZIP::minizip)
    elseif(TARGET minizip)
        set(MANGAPSP_MINIZIP_TARGET minizip)
    else()
        message(FATAL_ERROR "Fetched minizip-ng did not provide a recognized CMake target")
    endif()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MINIZIP REQUIRED IMPORTED_TARGET minizip>=3.0.4)
    set(MANGAPSP_MINIZIP_TARGET PkgConfig::MINIZIP)
    if(PSP)
        # Keep zlib an explicit PSP preflight dependency even though minizip's
        # pkg-config metadata also links it transitively.
        pkg_check_modules(ZLIB REQUIRED IMPORTED_TARGET zlib)
        set(MANGAPSP_ZLIB_TARGET PkgConfig::ZLIB)
    endif()
endif()

if(MANGAPSP_FETCH_JPEG)
    include(ExternalProject)
    set(MANGAPSP_JPEG_INSTALL_DIR "${CMAKE_BINARY_DIR}/jpeg-install")
    if(MSVC OR CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        set(MANGAPSP_JPEG_LIBRARY
            "${MANGAPSP_JPEG_INSTALL_DIR}/lib/jpeg-static${CMAKE_STATIC_LIBRARY_SUFFIX}")
    else()
        set(MANGAPSP_JPEG_LIBRARY
            "${MANGAPSP_JPEG_INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}jpeg${CMAKE_STATIC_LIBRARY_SUFFIX}")
    endif()
    file(MAKE_DIRECTORY "${MANGAPSP_JPEG_INSTALL_DIR}/include")
    ExternalProject_Add(mangapsp_jpeg_external
        URL https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/3.1.3.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        CMAKE_ARGS
            "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
            "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
            "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
            "-DCMAKE_C_COMPILER_ARG1=${CMAKE_C_COMPILER_ARG1}"
            "-DCMAKE_SIZEOF_VOID_P=${CMAKE_SIZEOF_VOID_P}"
            "-DCMAKE_AR=${CMAKE_AR}"
            "-DCMAKE_RANLIB=${CMAKE_RANLIB}"
            -DENABLE_SHARED=OFF
            -DENABLE_STATIC=ON
            -DWITH_TURBOJPEG=OFF
            -DWITH_TOOLS=OFF
            -DWITH_TESTS=OFF
            -DWITH_SIMD=OFF
        INSTALL_DIR "${MANGAPSP_JPEG_INSTALL_DIR}"
        BUILD_BYPRODUCTS "${MANGAPSP_JPEG_LIBRARY}"
    )
    add_library(MangaPSP_jpeg STATIC IMPORTED GLOBAL)
    set_target_properties(MangaPSP_jpeg PROPERTIES
        IMPORTED_LOCATION "${MANGAPSP_JPEG_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${MANGAPSP_JPEG_INSTALL_DIR}/include"
    )
    add_dependencies(MangaPSP_jpeg mangapsp_jpeg_external)
    set(MANGAPSP_JPEG_TARGET MangaPSP_jpeg)
else()
    if(PSP)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(JPEG REQUIRED IMPORTED_TARGET libjpeg)
        set(MANGAPSP_JPEG_TARGET PkgConfig::JPEG)
    else()
        find_package(JPEG REQUIRED)
        set(MANGAPSP_JPEG_TARGET JPEG::JPEG)
    endif()
endif()

add_library(MangaPSP_core STATIC
    src/FileScanner.cpp
    src/ImageDecoder.cpp
    src/ImageProbe.cpp
    src/JpegDecoder.cpp
    src/LibraryManager.cpp
    src/Log.cpp
    src/MemoryBudget.cpp
    src/NaturalSort.cpp
    src/PageSource.cpp
    src/PathUtils.cpp
    src/Persistence.cpp
    src/ReaderExperience.cpp
    src/SaveManager.cpp
    src/SettingsManager.cpp
    src/UiTheme.cpp
)

target_include_directories(MangaPSP_core PUBLIC src)
target_compile_definitions(MangaPSP_core PUBLIC
    MANGAPSP_PSP_RC=$<BOOL:${MANGAPSP_PSP_RC}>
    MANGAPSP_DEVELOPMENT=$<IF:$<CONFIG:Debug>,1,0>
    MANGAPSP_ENABLE_METRICS=$<IF:$<OR:$<CONFIG:Debug>,$<BOOL:${MANGAPSP_PSP_RC}>>,1,0>
    MANGAPSP_ENABLE_LOGGING=$<IF:$<OR:$<CONFIG:Debug>,$<BOOL:${MANGAPSP_PSP_RC}>>,1,0>
    MANGAPSP_HARDWARE_DIAGNOSTICS=$<IF:$<OR:$<CONFIG:Debug>,$<BOOL:${MANGAPSP_PSP_RC}>>,1,0>
    MANGAPSP_RUNTIME_DIAGNOSTICS=$<BOOL:${MANGAPSP_PSP_RC}>
    MANGAPSP_BOOT_DIAGNOSTICS=$<BOOL:${MANGAPSP_BOOT_DIAGNOSTICS}>
    MANGAPSP_BOOT_HOLD_STAGE=${MANGAPSP_BOOT_HOLD_STAGE}
    MANGAPSP_BOOT_HOLD_CHECKPOINT=${MANGAPSP_BOOT_HOLD_CHECKPOINT}
    MANGAPSP_DEFER_PSP_INPUT_SETUP=$<BOOL:${MANGAPSP_DEFER_PSP_INPUT_SETUP}>
    MANGAPSP_DISABLE_CBZ=$<BOOL:${MANGAPSP_DISABLE_CBZ}>
    MANGAPSP_DISABLE_THUMBNAILS=$<BOOL:${MANGAPSP_DISABLE_THUMBNAILS}>
    MANGAPSP_IGNORE_PERSISTENCE=$<BOOL:${MANGAPSP_IGNORE_PERSISTENCE}>
    MANGAPSP_FORCE_NO_MANGA=$<BOOL:${MANGAPSP_FORCE_NO_MANGA}>
    MANGAPSP_HOLD_AFTER_FOLDER_SCAN=$<BOOL:${MANGAPSP_HOLD_AFTER_FOLDER_SCAN}>
)
if(MANGAPSP_PSP_MEMORY_PROFILE STREQUAL "PSP_1000")
    target_compile_definitions(MangaPSP_core PUBLIC MANGAPSP_FORCE_PSP_32MB_PROFILE=1)
elseif(MANGAPSP_PSP_MEMORY_PROFILE STREQUAL "PSP_2000_PLUS")
    target_compile_definitions(MangaPSP_core PUBLIC MANGAPSP_FORCE_PSP_64MB_PROFILE=1)
endif()
target_link_libraries(MangaPSP_core PRIVATE
    ${MANGAPSP_MINIZIP_TARGET}
    ${MANGAPSP_JPEG_TARGET}
    ${MANGAPSP_ZLIB_TARGET}
)

if(MSVC)
    target_compile_definitions(MangaPSP_core PRIVATE _CRT_SECURE_NO_WARNINGS)
    # libjpeg's documented setjmp error boundary and ABI-aligned error struct
    # intentionally trigger these MSVC portability warnings.
    target_compile_options(MangaPSP_core PRIVATE /W4 /wd4324 /wd4611)
else()
    target_compile_options(MangaPSP_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MANGAPSP_BUILD_BENCHMARKS AND NOT PSP)
    add_executable(MangaPSP_jpeg_benchmark tests/JpegBenchmark.cpp)
    target_link_libraries(MangaPSP_jpeg_benchmark PRIVATE
        MangaPSP_core
        ${MANGAPSP_JPEG_TARGET}
    )
endif()

if(MANGAPSP_BUILD_APP)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    pkg_check_modules(SDL2_IMAGE REQUIRED IMPORTED_TARGET SDL2_image)
    pkg_check_modules(SDL2_GFX REQUIRED IMPORTED_TARGET SDL2_gfx)

    add_executable(MangaPSP
        src/main.cpp
        src/App.cpp
        src/BootDiagnostics.cpp
        src/ImageLoader.cpp
        src/Input.cpp
        src/MangaReader.cpp
        src/Platform.cpp
        src/ThumbnailCache.cpp
    )

    target_include_directories(MangaPSP PRIVATE src)
    target_link_libraries(MangaPSP PRIVATE
        MangaPSP_core
        PkgConfig::SDL2
        PkgConfig::SDL2_IMAGE
        PkgConfig::SDL2_GFX
    )

    if(NOT MSVC)
        target_link_libraries(MangaPSP PRIVATE m)
    endif()

    if(MSVC)
        target_compile_options(MangaPSP PRIVATE /W4)
    else()
        target_compile_options(MangaPSP PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(PSP)
        if(MANGAPSP_COMPILE_CBZ)
            message(STATUS "PSP dependencies: SDL2, SDL2_image, SDL2_gfx, libjpeg, minizip-ng, zlib")
        else()
            message(STATUS "PSP folder-only dependencies: SDL2, SDL2_image, SDL2_gfx, libjpeg (CBZ/minizip omitted)")
        endif()
        if(MANGAPSP_PSP_RC)
            message(STATUS "NexaManga PSP RC: optimized, all normal features enabled, lightweight diagnostics enabled")
        endif()
        message(STATUS "PSP memory profile: ${MANGAPSP_PSP_MEMORY_PROFILE}; PBP MEMSIZE=1")
        target_link_libraries(MangaPSP PRIVATE pspdebug)
        set(NEXAMANGA_PBP_ICON_ARGS)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ICON0.PNG")
            list(APPEND NEXAMANGA_PBP_ICON_ARGS ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/ICON0.PNG")
        elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Icon.png")
            list(APPEND NEXAMANGA_PBP_ICON_ARGS ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Icon.png")
        else()
            message(STATUS "NexaManga PSP: ICON0.PNG not present; packaging without an icon")
        endif()
        create_pbp_file(
            TARGET MangaPSP
            TITLE "NexaManga PSP"
            ${NEXAMANGA_PBP_ICON_ARGS}
            VERSION "01.00"
            MEMSIZE 1
        )
        if(MANGAPSP_PSP_RC)
            set(NEXAMANGA_RC_DIST_DIR
                "${CMAKE_CURRENT_SOURCE_DIR}/dist/psp-rc/NexaMangaPSP")
            add_custom_command(TARGET MangaPSP POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${NEXAMANGA_RC_DIST_DIR}"
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${CMAKE_CURRENT_BINARY_DIR}/EBOOT.PBP"
                    "${NEXAMANGA_RC_DIST_DIR}/EBOOT.PBP"
                COMMENT "Packaging NexaManga PSP RC distribution")
        endif()
    endif()
endif()

if(PSP AND MANGAPSP_BUILD_PSP_SMOKE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

    add_executable(MangaPSP_PSP_Smoke
        src/PspSmokeMain.cpp
        src/BootDiagnostics.cpp
    )
    target_include_directories(MangaPSP_PSP_Smoke PRIVATE src)
    target_compile_definitions(MangaPSP_PSP_Smoke PRIVATE
        MANGAPSP_DEVELOPMENT=1
        MANGAPSP_ENABLE_METRICS=0
        MANGAPSP_BOOT_DIAGNOSTICS=1
        MANGAPSP_BOOT_HOLD_STAGE=${MANGAPSP_BOOT_HOLD_STAGE}
    )
    target_compile_options(MangaPSP_PSP_Smoke PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(MangaPSP_PSP_Smoke PRIVATE PkgConfig::SDL2 pspdebug m)
    create_pbp_file(
        TARGET MangaPSP_PSP_Smoke
        TITLE "NexaManga PSP Smoke"
        VERSION "01.00"
        MEMSIZE 1
    )
endif()

include(CTest)
if(BUILD_TESTING AND NOT PSP)
    add_executable(MangaPSP_core_tests tests/CoreTests.cpp)
    target_link_libraries(MangaPSP_core_tests PRIVATE MangaPSP_core)
    add_test(NAME MangaPSP_core_tests COMMAND MangaPSP_core_tests)

    add_executable(MangaPSP_reader_experience_tests tests/ReaderExperienceTests.cpp)
    target_link_libraries(MangaPSP_reader_experience_tests PRIVATE MangaPSP_core)
    add_test(NAME MangaPSP_reader_experience_tests COMMAND MangaPSP_reader_experience_tests)

    add_executable(MangaPSP_page_source_tests tests/PageSourceTests.cpp)
    target_link_libraries(MangaPSP_page_source_tests PRIVATE MangaPSP_core)
    add_test(NAME MangaPSP_page_source_tests COMMAND MangaPSP_page_source_tests)

    add_executable(MangaPSP_page_cache_tests tests/PageCacheTests.cpp)
    target_link_libraries(MangaPSP_page_cache_tests PRIVATE MangaPSP_core)
    add_test(NAME MangaPSP_page_cache_tests COMMAND MangaPSP_page_cache_tests)

    add_executable(MangaPSP_jpeg_decoder_tests tests/JpegDecoderTests.cpp)
    target_link_libraries(MangaPSP_jpeg_decoder_tests PRIVATE
        MangaPSP_core
        ${MANGAPSP_JPEG_TARGET}
    )
    add_test(NAME MangaPSP_jpeg_decoder_tests COMMAND MangaPSP_jpeg_decoder_tests)
endif()
