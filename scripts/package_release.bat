@echo off
setlocal enabledelayedexpansion

REM ==============================================================================
REM Portable Linux Groovebox Framework — SD Card Release Packager (Windows)
REM Builds RK3326 (Cortex-A35) and A133P (Cortex-A53) binaries using Zig + Ninja
REM and assembles dist\Ports\ ready to copy onto a handheld SD card.
REM ==============================================================================

cd /d "%~dp0.."
set "ROOT_DIR=%CD%"
set "DIST_DIR=%ROOT_DIR%\dist"
set "PORTS_DIR=%DIST_DIR%\Ports"
set "APP_DIR=%PORTS_DIR%\groovebox"

echo ==^> [1/4] Cross-compiling AArch64 binary for Rockchip RK3326 (MagicX XU10 / R36S)...
cmake --preset rk3326-ninja
if errorlevel 1 exit /b 1
cmake --build --preset rk3326-ninja --target xu10_groovebox
if errorlevel 1 exit /b 1

echo ==^> [2/4] Cross-compiling AArch64 binary for Allwinner A133P (Trimui Brick / Brick Pro)...
cmake --preset a133p-ninja
if errorlevel 1 exit /b 1
cmake --build --preset a133p-ninja --target xu10_groovebox
if errorlevel 1 exit /b 1

echo ==^> [3/4] Assembling portable SD card bundle in %PORTS_DIR% ...
if exist "%DIST_DIR%" rmdir /s /q "%DIST_DIR%"
mkdir "%APP_DIR%\profiles"
mkdir "%APP_DIR%\oneshots"
mkdir "%APP_DIR%\loops"
mkdir "%APP_DIR%\dx7"
mkdir "%APP_DIR%\projects"
mkdir "%APP_DIR%\recordings"

copy /y "%ROOT_DIR%\build-xu10-win\xu10_groovebox"  "%APP_DIR%\xu10_groovebox_rk3326" >nul
copy /y "%ROOT_DIR%\build-a133p-win\xu10_groovebox" "%APP_DIR%\xu10_groovebox_a133p" >nul
copy /y "%ROOT_DIR%\build-xu10-win\xu10_groovebox"  "%APP_DIR%\xu10_groovebox" >nul

copy /y "%ROOT_DIR%\profiles\*.ini" "%APP_DIR%\profiles\" >nul
if exist "%ROOT_DIR%\res\samples\drums" xcopy /s /e /y /q "%ROOT_DIR%\res\samples\drums\*" "%APP_DIR%\oneshots\" >nul
if exist "%ROOT_DIR%\res\samples\loops" xcopy /s /e /y /q "%ROOT_DIR%\res\samples\loops\*" "%APP_DIR%\loops\" >nul
if exist "%ROOT_DIR%\res\dx7" copy /y "%ROOT_DIR%\res\dx7\*.syx" "%APP_DIR%\dx7\" >nul

powershell -NoProfile -Command ^
  "$lf = \"`n\";" ^
  "$s = '#!/bin/sh' + $lf + " ^
  "'PORT_DIR=\"$(CDPATH= cd -- \"$(dirname -- \"$0\")\" && pwd)\"' + $lf + " ^
  "'APP_DIR=\"$PORT_DIR/groovebox\"' + $lf + " ^
  "'cd \"$APP_DIR\" || exit 1' + $lf + " ^
  "'export GROOVEBOX_DATA_DIR=\"$APP_DIR\"' + $lf + " ^
  "'MODEL=\"\"' + $lf + " ^
  "'if [ -r /proc/device-tree/model ]; then MODEL=\"$(tr -d ''\0'' < /proc/device-tree/model)\"; fi' + $lf + " ^
  "'BIN=\"$APP_DIR/xu10_groovebox_rk3326\"' + $lf + " ^
  "'case \"$MODEL\" in *Trimui*|*TRIMUI*|*Brick*|*A133*) [ -x \"$APP_DIR/xu10_groovebox_a133p\" ] && BIN=\"$APP_DIR/xu10_groovebox_a133p\" ;; esac' + $lf + " ^
  "'if [ -z \"$DISPLAY\" ] && [ -z \"$WAYLAND_DISPLAY\" ]; then export SDL_VIDEODRIVER=\"${SDL_VIDEODRIVER:-kmsdrm}\"; fi' + $lf + " ^
  "'exec \"$BIN\" \"$@\"' + $lf;" ^
  "[System.IO.File]::WriteAllText('%PORTS_DIR%\Groovebox.sh', $s, (New-Object System.Text.UTF8Encoding($false)))"

echo ==^> [4/4] Done! Ready-to-copy SD card bundle: %PORTS_DIR%
