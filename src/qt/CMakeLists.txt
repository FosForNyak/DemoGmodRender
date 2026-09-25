# =============================================================================
#  Програма з вікном gmdr на Qt 6 (Qt Quick / QML). Логіка — у ядрі (gmdr_core) і
#  C++-сервісах тут; QML лише показує стан і передає дії.
#  Модулі Qt — лише потрібні: Core, Gui, Qml, Quick, QuickControls2.
# =============================================================================
qt_standard_project_setup()
if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)   # Qt 6.8+: qmldir і для підтек qml/ (components, workspaces)
endif()

file(GLOB GMDR_QT_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")
file(GLOB_RECURSE GMDR_QML_FILES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/qml/*.qml")

set(_extra)
if(WIN32)
    set(_extra app_qt.rc)
endif()
qt_add_executable(gmdr WIN32 ${GMDR_QT_SOURCES} ${_extra})
set_source_files_properties(qml/Theme.qml qml/Ui.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(gmdr
    URI Gmdr.Ui
    VERSION 1.0
    RESOURCE_PREFIX /
    QML_FILES ${GMDR_QML_FILES})
qt_add_resources(gmdr gmdr_assets PREFIX /gmdr FILES app_icon.png)

target_include_directories(gmdr PRIVATE "${CMAKE_SOURCE_DIR}/src")
# Перевірка Vulkan у «Налаштування → Графіка» потребує лише заголовків: Vulkan SDK або Khronos
# Vulkan-Headers у third_party/vulkan-headers (CI). Без них Vulkan показується «не перевірено».
find_path(GMDR_VULKAN_INCLUDE_DIR vulkan/vulkan.h
    HINTS "$ENV{VULKAN_SDK}/Include" "$ENV{VULKAN_SDK}/include" "${CMAKE_SOURCE_DIR}/third_party/vulkan-headers/include")
if(GMDR_VULKAN_INCLUDE_DIR)
    target_include_directories(gmdr PRIVATE "${GMDR_VULKAN_INCLUDE_DIR}")
endif()
target_link_libraries(gmdr PRIVATE gmdr_core Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
if(WIN32)
    target_link_libraries(gmdr PRIVATE ole32 shell32 user32 winmm)
    if(MSVC)
        target_link_options(gmdr PRIVATE /MANIFEST:NO)   # маніфест — у app_qt.rc
    endif()
endif()
# Поруч із gmdr-cli і DLL FFmpeg
set_target_properties(gmdr PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
gmdr_copy_ffmpeg_dlls(gmdr)
gmdr_copy_voice_model(gmdr)
