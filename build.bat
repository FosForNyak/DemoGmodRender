@echo off
chcp 65001 >nul
rem ============================================================================
rem  Збірка GMod Demo Render (потрібна Visual Studio 2022/2026 з компонентом
rem  "Desktop development with C++" - у ньому вже є CMake).
rem  Результат: build\Release\gmdr.exe (з вікном) і gmdr-cli.exe (консоль)
rem ============================================================================
setlocal
cd /d "%~dp0"

if not exist "third_party\ffmpeg\include\libavcodec\avcodec.h" (
    echo FFmpeg не знайдено - завантажую...
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\get_ffmpeg.ps1
    if errorlevel 1 goto :error
)

rem --- Шукаємо CMake: спершу той, що входить у Visual Studio, потім у PATH ---
set "CMAKE_EXE="
set "VSINSTALLER=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
if not exist "%VSINSTALLER%\vswhere.exe" goto :cmake_from_path
rem (через pushd, бо шлях з "(x86)" у лапках всередині for /f ламає cmd)
pushd "%VSINSTALLER%"
for /f "usebackq delims=" %%i in (`.\vswhere.exe -latest -products * -find Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`) do set "CMAKE_EXE=%%i"
popd
if defined CMAKE_EXE goto :have_cmake
:cmake_from_path
where cmake >nul 2>nul
if errorlevel 1 goto :no_cmake
set "CMAKE_EXE=cmake"

:have_cmake
echo CMake: %CMAKE_EXE%
"%CMAKE_EXE%" -S . -B build -A x64
if errorlevel 1 goto :error
"%CMAKE_EXE%" --build build --config Release --parallel
if errorlevel 1 goto :error

echo.
echo Готово! Програма: build\Release\gmdr.exe
pause
exit /b 0

:no_cmake
echo Не знайдено CMake. Встановіть Visual Studio 2022/2026 з компонентом
echo "Desktop development with C++" (https://visualstudio.microsoft.com/) або CMake (https://cmake.org).

:error
echo.
echo Збірка не вдалася.
pause
exit /b 1
