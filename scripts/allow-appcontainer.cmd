@echo off
rem Lets AppContainer apps (the gmdr-import sandbox) read and load files in the given folders.
rem Usually NOT needed: the importer's image is opened with the parent's rights and it loads only System32
rem DLLs. Use it only if the importer's folder holds DLLs it must load (e.g. an app-local VC++ runtime).
rem Adds one inheritable ACE for ALL APPLICATION PACKAGES (S-1-15-2-1): read & execute, nothing else.
rem Program Files already has it; this is for development and CI build folders.
rem Undo: icacls "<folder>" /remove *S-1-15-2-1 /T
rem Usage: scripts\allow-appcontainer.cmd <folder> [folder...]
setlocal
if "%~1"=="" (
  echo usage: %~nx0 ^<folder^> [folder...] 1>&2
  exit /b 64
)
:next
if "%~1"=="" exit /b 0
if not exist "%~1\" (
  echo not a folder: %~1 1>&2
  exit /b 1
)
icacls "%~1" /grant "*S-1-15-2-1:(OI)(CI)(RX)" /T /Q >nul || exit /b 1
echo AppContainer apps may read and run programs in %~1
shift
goto next
