@echo off
REM Fairy Tapes - Build script for Windows.
REM Usage:
REM   build.bat              Build all plugins (tests first, then build).
REM   build.bat the-stripper Build only The Stripper; tests run first.
REM   build.bat --configure  Only run CMake configure, no build.
REM
REM Requires: CMake in PATH, and a generator (e.g. Visual Studio or Ninja).
REM Optional: run from "x64 Native Tools Command Prompt for VS" for Visual Studio.

set SUITE_ROOT=%~dp0
set SUITE_ROOT=%SUITE_ROOT:~0,-1%
set BUILD_DIR=%SUITE_ROOT%\build

if "%1"=="" goto build_all
if "%1"=="--configure" goto configure_only
if "%1"=="the-stripper" goto build_stripper
echo Unknown argument: %1. Use: build.bat [the-stripper^|--configure]
exit /b 1

:configure_only
echo Configuring CMake in %BUILD_DIR%...
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"
cmake ..
cd /d "%SUITE_ROOT%"
exit /b 0

:build_stripper
echo Building and running tests...
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"
if not exist CMakeCache.txt cmake ..
cmake --build . --target the_stripper_tests --config Release 2>nul
if errorlevel 1 cmake --build . --target the_stripper_tests
ctest --output-on-failure -C Release 2>nul
if errorlevel 1 ctest --output-on-failure
if errorlevel 1 (
  echo Tests failed. Aborting build.
  cd /d "%SUITE_ROOT%"
  exit /b 1
)
echo Building plugin target: the_stripper
cmake --build . --target the_stripper --config Release 2>nul
if errorlevel 1 cmake --build . --target the_stripper
cd /d "%SUITE_ROOT%"
echo Done. Plugin: %SUITE_ROOT%\dist\The Stripper.vst3
exit /b 0

:build_all
echo Configuring CMake in %BUILD_DIR%...
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"
if not exist CMakeCache.txt cmake ..
echo Building and running tests...
cmake --build . --target the_stripper_tests --config Release 2>nul
if errorlevel 1 cmake --build . --target the_stripper_tests
ctest --output-on-failure -C Release 2>nul
if errorlevel 1 ctest --output-on-failure
if errorlevel 1 (
  echo Tests failed. Aborting build.
  cd /d "%SUITE_ROOT%"
  exit /b 1
)
echo Building all plugins...
cmake --build . --config Release 2>nul
if errorlevel 1 cmake --build .
cd /d "%SUITE_ROOT%"
echo Done.
exit /b 0
