# =============================================================================
#  FindFFmpeg.cmake — пошук бібліотек FFmpeg (avformat, avcodec, avutil,
#  swscale, swresample).
#
#  Способи:
#   1) -DFFMPEG_ROOT=<папка>   — папка зі збіркою "shared" (include/, lib/, bin/),
#      наприклад розпакований архів BtbN ffmpeg-n8.1-latest-win64-gpl-shared-8.1.
#      Типово шукаємо в third_party/ffmpeg.
#   2) pkg-config (Linux).
#
#  Результат: імпортовані цілі FFmpeg::avformat, FFmpeg::avcodec, ...,
#  змінна FFMPEG_DLL_DIR (Windows) — звідки копіювати DLL.
# =============================================================================
set(_ff_components avformat avcodec avutil swscale swresample)

if(NOT FFMPEG_ROOT AND EXISTS "${CMAKE_SOURCE_DIR}/third_party/ffmpeg/include/libavcodec/avcodec.h")
    set(FFMPEG_ROOT "${CMAKE_SOURCE_DIR}/third_party/ffmpeg")
endif()

set(FFmpeg_FOUND TRUE)

if(FFMPEG_ROOT)
    message(STATUS "FFmpeg: використовую FFMPEG_ROOT=${FFMPEG_ROOT}")
    find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h PATHS "${FFMPEG_ROOT}/include" NO_DEFAULT_PATH)
    if(NOT FFMPEG_INCLUDE_DIR)
        message(FATAL_ERROR "У ${FFMPEG_ROOT}/include не знайдено заголовків FFmpeg")
    endif()
    foreach(comp IN LISTS _ff_components)
        find_library(FFMPEG_${comp}_LIBRARY NAMES ${comp} lib${comp} ${comp}.dll
                     PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)
        if(NOT FFMPEG_${comp}_LIBRARY)
            message(FATAL_ERROR "Не знайдено бібліотеку ${comp} у ${FFMPEG_ROOT}/lib")
        endif()
        if(NOT TARGET FFmpeg::${comp})
            add_library(FFmpeg::${comp} UNKNOWN IMPORTED)
            set_target_properties(FFmpeg::${comp} PROPERTIES
                IMPORTED_LOCATION "${FFMPEG_${comp}_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}")
        endif()
    endforeach()
    if(EXISTS "${FFMPEG_ROOT}/bin")
        set(FFMPEG_DLL_DIR "${FFMPEG_ROOT}/bin")
    endif()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PC_FFMPEG REQUIRED IMPORTED_TARGET
        libavformat libavcodec libavutil libswscale libswresample)
    foreach(comp IN LISTS _ff_components)
        if(NOT TARGET FFmpeg::${comp})
            add_library(FFmpeg::${comp} INTERFACE IMPORTED)
            set_target_properties(FFmpeg::${comp} PROPERTIES INTERFACE_LINK_LIBRARIES PkgConfig::PC_FFMPEG)
        endif()
    endforeach()
endif()
