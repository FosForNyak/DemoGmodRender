# =============================================================================
#  GMod Demo Render — рендер демо-записів Garry's Mod у відео.
#
#  Збірка (Windows, Visual Studio 2022):
#     1) Завантажте FFmpeg:  powershell -ExecutionPolicy Bypass -File scripts\get_ffmpeg.ps1
#     2) Встановіть Qt 6.8 (msvc2022_64) і вкажіть його CMake: CMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
#     3) Відкрийте папку проекту у Visual Studio ("Відкрити локальну папку")
#        або виконайте build.bat
#
#  Цілі:
#     gmdr       — програма з вікном (Qt 6 / Qt Quick, src/qt; без Qt не збирається)
#     gmdr-cli   — консольна версія (для автоматизації та тестів)
#     gmdr_core  — ядро (статична бібліотека)
# =============================================================================
cmake_minimum_required(VERSION 3.20)
if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)   # FFMPEG_ROOT як шлях пошуку FFmpeg
endif()
project(GModDemoRender VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
# Версія — лише тут: у програмі (GMDR_VERSION), у звіті і в ресурсі .exe (src/qt/app_qt.rc)
add_compile_definitions(GMDR_VERSION="${PROJECT_VERSION}" GMDR_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
                        GMDR_VERSION_MINOR=${PROJECT_VERSION_MINOR} GMDR_VERSION_PATCH=${PROJECT_VERSION_PATCH})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

option(GMDR_BUILD_GUI   "Збирати програму з вікном (Qt 6, якщо знайдено)" ON)
option(GMDR_BUILD_CLI   "Збирати консольну програму" ON)
option(GMDR_BUILD_TESTS "Збирати тести" OFF)
option(GMDR_BUILD_FUZZERS "Збирати фазер парсера демо (libFuzzer; Clang або MSVC)" OFF)
set(GMDR_SANITIZE "" CACHE STRING "Санітайзери: address (MSVC/GCC/Clang), address,undefined (GCC/Clang)")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(MSVC)
    # /utf-8 — вихідні файли у UTF-8 (українські рядки!), /MP — паралельна компіляція
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:/utf-8;/W3;/MP;/permissive-;/Zc:__cplusplus>")
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
else()
    # -Werror=format: помилки у форматних рядках (ImGui::Text, printf) — це невизначена поведінка
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:-Wall;-Wextra;-Wno-unused-parameter;-Wno-missing-field-initializers;-Werror=format>")
    if(WIN32)
        add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    endif()
endif()

if(WIN32)
    # Windows 10+: потрібні API (SetFileInformationByHandle, EcoQoS...) і в заголовках MinGW
    add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00)
endif()

# Санітайзери (ловлять вихід за межі пам'яті тощо) — для перевірки тестами і фазером
if(GMDR_SANITIZE)
    if(MSVC)
        add_compile_options(/fsanitize=address /Zi)
        add_link_options(/INCREMENTAL:NO /DEBUG)
    else()
        add_compile_options(-fsanitize=${GMDR_SANITIZE} -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=${GMDR_SANITIZE})
    endif()
endif()

find_package(Threads REQUIRED)
find_package(FFmpeg REQUIRED)

# ---- Ядро ---------------------------------------------------------------------
file(GLOB_RECURSE GMDR_CORE_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/src/core/*.cpp" "${CMAKE_SOURCE_DIR}/src/core/*.hpp")
add_library(gmdr_core STATIC ${GMDR_CORE_SOURCES})
target_include_directories(gmdr_core PUBLIC "${CMAKE_SOURCE_DIR}/src")

# Lua-драйвер і помічник синтезу мовлення вбудовуються в програму як масиви байтів (щоб .exe
# був самодостатнім). Масив, а не рядковий літерал: у MSVC літерал не може бути довшим за ~16 КБ.
function(gmdr_embed_text src inc_name)
    file(READ "${src}" _hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _bytes "${_hex}")
    string(REPLACE "0x0a," "0x0a,\n" _bytes "${_bytes}")   # рядок файлу = рядок масиву
    file(WRITE "${CMAKE_BINARY_DIR}/generated/${inc_name}.tmp" "${_bytes}0x00\n")
    configure_file("${CMAKE_BINARY_DIR}/generated/${inc_name}.tmp" "${CMAKE_BINARY_DIR}/generated/${inc_name}" COPYONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${src}")
endfunction()
set(GMDR_DRIVER_LUA "${CMAKE_SOURCE_DIR}/src/core/game/gmdr_driver.lua")
gmdr_embed_text("${GMDR_DRIVER_LUA}" gmdr_driver_lua.inc)
gmdr_embed_text("${CMAKE_SOURCE_DIR}/src/core/dub/gmdr_voice.py" gmdr_voice_py.inc)
target_include_directories(gmdr_core PRIVATE "${CMAKE_BINARY_DIR}/generated"
                           "${CMAKE_SOURCE_DIR}/third_party/stb")   # stb_truetype для підписів на кадрі
target_link_libraries(gmdr_core PUBLIC
    FFmpeg::avformat FFmpeg::avcodec FFmpeg::avfilter FFmpeg::swscale FFmpeg::swresample FFmpeg::avutil
    Threads::Threads)
if(WIN32)
    target_link_libraries(gmdr_core PUBLIC advapi32 shell32 ole32 user32 powrprof winhttp crypt32)
endif()

# Функція: скопіювати DLL FFmpeg поруч з .exe (Windows)
function(gmdr_copy_ffmpeg_dlls target)
    if(WIN32 AND FFMPEG_DLL_DIR)
        file(GLOB _dlls "${FFMPEG_DLL_DIR}/*.dll")
        list(FILTER _dlls EXCLUDE REGEX "avdevice")   # libavdevice не потрібна ні програмі, ні іншим DLL
        if(_dlls)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_dlls} "$<TARGET_FILE_DIR:${target}>"
                COMMENT "Копіюю DLL FFmpeg")
        endif()
    endif()
endfunction()

# Модель RNNoise для шумодава голосу — поруч із програмою (без неї шумодав бере afftdn)
function(gmdr_copy_voice_model target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/third_party/rnnoise-models/lq.rnnn"
                "$<TARGET_FILE_DIR:${target}>/rnnoise-voice.rnnn")
endfunction()

# ---- Консольна програма ---------------------------------------------------------
if(GMDR_BUILD_CLI)
    add_executable(gmdr-cli src/cli/main_cli.cpp)
    target_link_libraries(gmdr-cli PRIVATE gmdr_core)
    gmdr_copy_ffmpeg_dlls(gmdr-cli)
    gmdr_copy_voice_model(gmdr-cli)
endif()

# ---- Програма з вікном (Qt 6 / Qt Quick) -------------------------------------------
if(GMDR_BUILD_GUI)
    find_package(Qt6 6.4 QUIET COMPONENTS Core Gui Qml Quick QuickControls2)
    if(Qt6_FOUND)
        add_subdirectory(src/qt)
    else()
        message(STATUS "Qt 6.4+ не знайдено — програма з вікном не збирається, лише gmdr-cli (див. README, «Збирання»)")
    endif()
endif()

# ---- Тести --------------------------------------------------------------------
if(GMDR_BUILD_TESTS)
    enable_testing()
    add_executable(gmdr-tests tests/test_main.cpp tests/test_config.cpp)
    target_link_libraries(gmdr-tests PRIVATE gmdr_core)
    add_test(NAME unit COMMAND gmdr-tests)
    gmdr_copy_voice_model(gmdr-tests)
    add_executable(gmdr-fakegame tests/fake_game.cpp)
    target_link_libraries(gmdr-fakegame PRIVATE gmdr_core)
    add_executable(gmdr-fakewhisper tests/fake_whisper.cpp)
    target_link_libraries(gmdr-fakewhisper PRIVATE gmdr_core)
endif()

# ---- Фазер ----------------------------------------------------------------------
if(GMDR_BUILD_FUZZERS)
    add_executable(gmdr-fuzz-demo tests/fuzz_demo.cpp)
    target_link_libraries(gmdr-fuzz-demo PRIVATE gmdr_core)
    if(MSVC)
        target_compile_options(gmdr-fuzz-demo PRIVATE /fsanitize=fuzzer)
        target_link_options(gmdr-fuzz-demo PRIVATE /fsanitize=fuzzer)
    else()
        target_compile_options(gmdr-fuzz-demo PRIVATE -fsanitize=fuzzer)
        target_link_options(gmdr-fuzz-demo PRIVATE -fsanitize=fuzzer)
    endif()
    gmdr_copy_ffmpeg_dlls(gmdr-fuzz-demo)
endif()
