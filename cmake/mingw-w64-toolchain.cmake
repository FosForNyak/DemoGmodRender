# Крос-компіляція під Windows з Linux (MinGW-w64). Для Visual Studio не потрібен.
#   cmake -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-toolchain.cmake -DFFMPEG_ROOT=<ffmpeg win64 shared> ..
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
find_program(_mingw_gcc NAMES x86_64-w64-mingw32-gcc-posix x86_64-w64-mingw32-gcc)
find_program(_mingw_gxx NAMES x86_64-w64-mingw32-g++-posix x86_64-w64-mingw32-g++)
set(CMAKE_C_COMPILER ${_mingw_gcc})
set(CMAKE_CXX_COMPILER ${_mingw_gxx})
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
# Статичне лінкування рантайму C++ — щоб .exe не вимагав libstdc++-6.dll тощо
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
