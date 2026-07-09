@echo off
setlocal enabledelayedexpansion
pushd "%~dp0"
echo ========================================
echo  Dispersed - Full Build Script
echo ========================================
echo.

:: 1. BUILD WINDOWS
echo [1/3] Building for Windows...
if exist build_windows rmdir /s /q build_windows
mkdir build_windows
cd build_windows
cmake .. -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build . --config Release
cd ..

:: 2. BUILD WEB
echo [2/3] Building for Web...
call "%~dp0emsdk\emsdk_env.bat"

:: Add vcpkg's downloaded ninja to PATH so CMake can find it
set "VCPKG_NINJA_DIR="
for /d %%d in ("%~dp0vcpkg\downloads\tools\ninja-*") do (
    if exist "%%d\ninja.exe" set "VCPKG_NINJA_DIR=%%d"
)
if defined VCPKG_NINJA_DIR (
    set "PATH=!VCPKG_NINJA_DIR!;!PATH!"
)

:: Clear corrupted vcpkg compiler cache
if exist "C:\Users\daeli\Documents\vcpkg\buildtrees\detect_compiler" (
    echo -- Clearing corrupted vcpkg compiler cache...
    rmdir /s /q "C:\Users\daeli\Documents\vcpkg\buildtrees\detect_compiler"
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
echo -- Found Emscripten toolchain: !EMSCRIPTEN_TOOLCHAIN!

if exist build_web rmdir /s /q build_web
mkdir build_web
cd build_web

cmake .. -G Ninja ^
    -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake ^
    -DVCPKG_TARGET_TRIPLET=wasm32-emscripten ^
    -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=!EMSCRIPTEN_TOOLCHAIN!

cmake --build . --config Release
cd ..

:: 3. COPY ASSETS FOR WINDOWS EXEs (The TTF Fix)
echo [3/3] Copying assets directly next to Windows EXEs...
:: Engine Assets -> Next to Editor.exe
if exist src\assets (
xcopy /E /I /Y src\assets build_windows\src\Release\assets >nul 2>&1
echo - Copied Engine assets to src\Release
)
:: Project Assets -> Loop through all projects dynamically
for /d %%p in (projects\*) do (
    if exist "%%p\assets" (
        xcopy /E /I /Y "%%p\assets" "build_windows\%%p\Release\assets" >nul 2>&1
        echo - Copied Project assets to %%~nxp\Release
    )
)
:: Copy vcpkg DLLs just in case
xcopy /Y vcpkg_installed\x64-windows\bin\*.dll build_windows\src\Release\ >nul 2>&1
for /d %%p in (projects\*) do (
    xcopy /Y vcpkg_installed\x64-windows\bin\*.dll "build_windows\%%p\Release\" >nul 2>&1
)

echo.
echo ========================================
echo  BUILD COMPLETE
echo ========================================
echo.
echo Windows Engine:  build_windows\src\Release\Editor.exe
echo Windows Projects:
for /d %%p in (projects\*) do echo - build_windows\%%p\Release\%%~nxp.exe
echo.
echo Web Files (Run with 'emrun'):
for /r build_web %%f in (*.html) do echo - %%f
echo.
pause
popd