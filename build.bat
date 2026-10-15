@echo off
rem Configure + build CaliforniaSpeedRecompiled (Release, into build/) with Visual Studio's environment,
rem LLVM clang-cl and Ninja. Machine-specific paths: see tools\build_env.bat.
rem Usage: build.bat [configure|build]   (default: both)
cd /d "%~dp0"
call "%~dp0tools\build_env.bat" || exit /b 1
if "%1"=="build" goto build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl %PATCHES_FLAGS% || exit /b 1
if "%1"=="configure" exit /b 0
:build
cmake --build build --target CaliforniaSpeedRecompiled
