@echo off
rem Builds the C++ engine with the Visual Studio toolchain (bundled CMake, Ninja and vcpkg).
rem Usage: scripts\build-engine.cmd [preset] [targets...]    preset defaults to windows-msvc-release
setlocal
set PRESET=%1
if "%PRESET%"=="" set PRESET=windows-msvc-release

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if "%VSDIR%"=="" (
  echo Visual Studio with C++ tools was not found. 1>&2
  exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if "%VCPKG_ROOT%"=="" set VCPKG_ROOT=%VSDIR%\VC\vcpkg

cd /d "%~dp0..\engine" || exit /b 1
if not exist "build\%PRESET%\build.ninja" (
  cmake --preset %PRESET% || exit /b 1
)
shift
if "%1"=="" (
  cmake --build --preset %PRESET% || exit /b 1
) else (
  cmake --build --preset %PRESET% --target %1 %2 %3 %4 %5 || exit /b 1
)
