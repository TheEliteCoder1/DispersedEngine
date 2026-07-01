@echo off
setlocal enabledelayedexpansion
pushd "%~dp0"
echo ========================================
echo  Dispersed - Fast Incremental Build
echo ========================================
echo.

:: Parse optional arguments: --windows-only, --web-only
set BUILD_WINDOWS=1
set BUILD_WEB=1
:parse_args
if "%~1"=="--windows-only" (
set BUILD_WEB=0
shift
goto parse_args
)
if "%~1"=="--web-only" (
set BUILD_WINDOWS=0
shift
goto parse_args
)

:: -----------------------------------------------------------------------
:: 1. BUILD WINDOWS (incremental)
:: -----------------------------------------------------------------------
if "%BUILD_WINDOWS%"=="1" (
echo [Step 1] Building for Windows (incremental^)...
:: First-time setup: run CMake configure only if cache is missing
if not exist build_windows\CMakeCache.txt (
echo   -- No CMake cache found. Running first-time configure...
mkdir build_windows 2>nul
cd build_windows
cmake .. -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
if errorlevel 1 (
echo [ERROR] Windows CMake configure failed.
cd ..
goto end
)
cd ..
) else (
echo   -- Cache found. Skipping configure.
)

:: Incremental build: CMake/MSBuild only recompiles changed translation units
cd build_windows
cmake --build . --config Release
if errorlevel 1 (
echo [ERROR] Windows build failed.
cd ..
goto end
)
cd ..
echo   -- Windows build complete.
) else (
echo [Step 1] Skipped Windows build (--web-only^).
)
echo.

:: -----------------------------------------------------------------------
:: 2. BUILD WEB (incremental)
:: -----------------------------------------------------------------------
if "%BUILD_WEB%"=="1" (
echo [Step 2] Building for Web (incremental^)...
:: Activate Emscripten environment
call "%~dp0emsdk\emsdk_env.bat"

:: Add vcpkg's downloaded ninja to PATH so CMake can find it
set "VCPKG_NINJA_DIR="
for /d %%d in ("%~dp0vcpkg\downloads\tools\ninja-*") do (
    if exist "%%d\ninja.exe" set "VCPKG_NINJA_DIR=%%d"
)
if defined VCPKG_NINJA_DIR (
    set "PATH=!VCPKG_NINJA_DIR!;!PATH!"
)

:: FIX: Dynamically find Emscripten.cmake to support both old and new emsdk layouts
set "EMSCRIPTEN_TOOLCHAIN="
for /f "delims=" %%f in ('dir /s /b "%~dp0emsdk\*Emscripten.cmake" 2^>nul ^| findstr /i "Platform"') do (
    if "!EMSCRIPTEN_TOOLCHAIN!"=="" set "EMSCRIPTEN_TOOLCHAIN=%%f"
)
if not defined EMSCRIPTEN_TOOLCHAIN (
    echo [ERROR] Could not find Emscripten.cmake in emsdk folder.
    pause
    exit /b 1
)
:: Convert backslashes to forward slashes for CMake
set "EMSCRIPTEN_TOOLCHAIN=!EMSCRIPTEN_TOOLCHAIN:\=/!"
echo   -- Found Emscripten toolchain: !EMSCRIPTEN_TOOLCHAIN!

:: First-time setup: run CMake configure only if cache is missing
if not exist build_web\CMakeCache.txt (
echo   -- No CMake cache found. Running first-time configure...
mkdir build_web 2>nul
cd build_web
cmake .. -G Ninja ^
    -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake ^
    -DVCPKG_TARGET_TRIPLET=wasm32-emscripten ^
    -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=!EMSCRIPTEN_TOOLCHAIN!
if errorlevel 1 (
echo [ERROR] Web CMake configure failed.
cd ..
goto end
)
cd ..
) else (
echo   -- Cache found. Skipping configure.
)

:: Incremental build: Ninja only recompiles changed translation units
cd build_web
cmake --build . --config Release
if errorlevel 1 (
echo [ERROR] Web build failed.
cd ..
goto end
)
cd ..
echo   -- Web build complete.
) else (
echo [Step 2] Skipped Web build (--windows-only^).
)
echo.

:: -----------------------------------------------------------------------
:: 3. COPY ASSETS (only if source is newer than destination)
:: -----------------------------------------------------------------------
echo [Step 3] Syncing assets (changed files only^)...
:: Engine Assets -> Next to Editor.exe
if exist src\assets (
xcopy /E /I /Y /D src\assets build_windows\src\Release\assets >nul 2>&1
echo   -- Engine assets synced to src\Release
)
:: Project Assets -> Next to FirstProject.exe
if exist projects\FirstProject\assets (
xcopy /E /I /Y /D projects\FirstProject\assets build_windows\projects\FirstProject\Release\assets >nul 2>&1
echo   -- Project assets synced to FirstProject\Release
)
:: vcpkg DLLs (unchanged between builds, /D skips if already present and identical)
xcopy /Y /D vcpkg_installed\x64-windows\bin\*.dll build_windows\src\Release\ >nul 2>&1
xcopy /Y /D vcpkg_installed\x64-windows\bin\*.dll build_windows\projects\FirstProject\Release\ >nul 2>&1
echo   -- vcpkg DLLs checked.
echo.

echo ========================================
echo  FAST BUILD COMPLETE
echo ========================================
echo.
echo Tip: Run with --windows-only or --web-only to skip one platform.
echo      Run build.bat for a clean full rebuild from scratch.
echo.
if "%BUILD_WINDOWS%"=="1" (
echo Windows Engine:  build_windows\src\Release\Editor.exe
echo Windows Project: build_windows\projects\FirstProject\Release\FirstProject.exe
echo.
)
if "%BUILD_WEB%"=="1" (
echo Web Files (Run with 'emrun'^):
for /r build_web %%f in (*.html) do echo - %%f
echo.
)

:end
pause
popd