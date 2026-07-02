@echo off
setlocal enableextensions
chcp 65001 >nul
title convnet-qt Windows build

REM ==================== EDIT THESE IF NEEDED ====================
REM Qt install path (MSVC 2022 64-bit). If missing, auto-search C:\Qt\6.*
set "QT_DIR=C:\Qt\6.11.1\msvc2022_64"
REM vcpkg root
set "VCPKG_ROOT=C:\vcpkg"
REM Build dir (Windows only; do NOT share with WSL build-linux)
set "BUILD_DIR=build-win"
REM =============================================================

cd /d "%~dp0"

REM "build-win.bat clean" wipes the build dir first
if /i "%~1"=="clean" (
    echo [clean] removing %BUILD_DIR% ...
    rmdir /s /q "%BUILD_DIR%" 2>nul
)

echo.
echo ===== [1/6] Check Qt =====
if exist "%QT_DIR%\bin\windeployqt.exe" goto :qt_ok
echo   default QT_DIR not found, searching C:\Qt\6.*\msvc2022_64 ...
for /d %%d in ("C:\Qt\6.*") do (
    if exist "%%d\msvc2022_64\bin\windeployqt.exe" set "QT_DIR=%%d\msvc2022_64"
)
if exist "%QT_DIR%\bin\windeployqt.exe" goto :qt_ok
echo [ERROR] Qt6 (MSVC 2022 64-bit) not found.
echo   Install via Qt online installer, or edit QT_DIR at top of this script.
goto :fail
:qt_ok
echo   Qt: %QT_DIR%

echo.
echo ===== [2/6] Check vcpkg =====
if exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" goto :vcpkg_ok
echo [ERROR] vcpkg not found at %VCPKG_ROOT%
echo   git clone https://github.com/microsoft/vcpkg C:\vcpkg
echo   C:\vcpkg\bootstrap-vcpkg.bat
goto :fail
:vcpkg_ok
echo   vcpkg: %VCPKG_ROOT%

echo.
echo ===== [3/6] Check Visual Studio (C++) =====
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSPATH="
if not exist "%VSWHERE%" goto :vs_where_notfound
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do set "VSPATH=%%i"
:vs_where_notfound
REM fallback to common locations if vswhere fails
if defined VSPATH goto :vs_found
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" set "VSPATH=C:\Program Files\Microsoft Visual Studio\2022\Community"
if defined VSPATH goto :vs_found
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" set "VSPATH=C:\Program Files\Microsoft Visual Studio\2022\Professional"
if defined VSPATH goto :vs_found
if exist "D:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" set "VSPATH=D:\Program Files\Microsoft Visual Studio\18\Insiders"
if defined VSPATH goto :vs_found
echo [ERROR] Visual Studio with C++ tools not found.
echo   Install "Build Tools for Visual Studio 2022" with "Desktop development with C++".
goto :fail
:vs_found
echo   VS: %VSPATH%
echo   initializing MSVC x64 environment ...
REM vcvarsall.bat may overwrite VCPKG_ROOT; save it first.
set "USER_VCPKG_ROOT=%VCPKG_ROOT%"
call "%VSPATH%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
set "VCPKG_ROOT=%USER_VCPKG_ROOT%"
if errorlevel 1 goto :vcvarsall_fail
goto :vcvarsall_ok
:vcvarsall_fail
echo [ERROR] failed to initialize MSVC environment.
goto :fail
:vcvarsall_ok

echo.
echo ===== [4/6] Ensure libdatachannel (vcpkg) =====
if exist "%VCPKG_ROOT%\installed\x64-windows\include\rtc\rtc.hpp" goto :libdatachannel_ok
echo   not installed, running vcpkg install libdatachannel:x64-windows (slow first time) ...
pushd "%VCPKG_ROOT%"
".\vcpkg.exe" install libdatachannel:x64-windows
set "VCPKG_ERROR=%errorlevel%"
popd
if "%VCPKG_ERROR%"=="1" goto :fail
goto :libdatachannel_done
:libdatachannel_ok
echo   already installed.
:libdatachannel_done

echo.
echo ===== [5/6] CMake configure =====
cmake -B "%BUILD_DIR%" -S . -G Ninja "-DCMAKE_BUILD_TYPE=Release" "-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake" "-DCMAKE_PREFIX_PATH=%QT_DIR%"
if errorlevel 1 goto :cmake_fail
goto :cmake_ok
:cmake_fail
echo [ERROR] configure failed. If it says CMakeCache path mismatch, run: build-win.bat clean
goto :fail
:cmake_ok

echo.
echo ===== [6/6] Build (Release) =====
cmake --build "%BUILD_DIR%"
if errorlevel 1 goto :fail

set "EXE=%BUILD_DIR%\convnet-qt.exe"
if exist "%EXE%" goto :exe_ok
echo [ERROR] build finished but %EXE% not found
goto :fail
:exe_ok

echo.
echo ===== Deploy Qt DLLs =====
"%QT_DIR%\bin\windeployqt.exe" "%EXE%"

REM Copy wintun.dll next to exe if present in script dir (for the virtual NIC)
if exist "%~dp0wintun.dll" (
    copy /y "%~dp0wintun.dll" "%BUILD_DIR%\" >nul
    echo   copied wintun.dll
)

echo.
echo ========================================
echo [OK] output: %EXE%
echo   run: double-click, or "%EXE%" from cmd
echo   virtual NIC: put wintun.dll beside exe + run as Administrator
echo ========================================
goto :end

:fail
echo.
echo [FAILED] see error above.
endlocal
exit /b 1

:end
endlocal
exit /b 0
