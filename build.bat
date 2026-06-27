@echo off
setlocal enabledelayedexpansion
pushd "%~dp0"

echo ========================================
echo  Dispersed2D - Final Build Script
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
if exist build_web rmdir /s /q build_web
mkdir build_web
cd build_web
cmake .. -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=%~dp0emsdk\upstream\emscripten\cmake\Modules\Platform\Emscripten.cmake -DVCPKG_TARGET_TRIPLET=wasm32-emscripten -G Ninja
cmake --build . --config Release
cd ..

:: 3. COPY ASSETS FOR WINDOWS EXEs (The TTF Fix)
echo [3/3] Copying assets directly next to Windows EXEs...

:: Engine Assets -> Next to Editor.exe
if exist src\assets (
    xcopy /E /I /Y src\assets build_windows\src\Release\assets >nul 2>&1
    echo - Copied Engine assets to src\Release
)

:: Project Assets -> Next to FirstProject.exe
if exist projects\FirstProject\assets (
    xcopy /E /I /Y projects\FirstProject\assets build_windows\projects\FirstProject\Release\assets >nul 2>&1
    echo - Copied Project assets to FirstProject\Release
)

:: Copy vcpkg DLLs just in case
xcopy /Y vcpkg_installed\x64-windows\bin\*.dll build_windows\src\Release\ >nul 2>&1
xcopy /Y vcpkg_installed\x64-windows\bin\*.dll build_windows\projects\FirstProject\Release\ >nul 2>&1

echo.
echo ========================================
echo  BUILD COMPLETE
echo ========================================
echo.
echo Windows Engine:  build_windows\src\Release\Editor.exe
echo Windows Project: build_windows\projects\FirstProject\Release\FirstProject.exe
echo.
echo Web Files (Run with 'emrun'):
for /r build_web %%f in (*.html) do echo - %%f
echo.
pause
popd