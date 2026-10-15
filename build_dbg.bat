@echo off
rem Configure + build CaliforniaSpeedRecompiled (RelWithDebInfo, into build-dbg/) with Visual Studio's
rem environment, LLVM clang-cl and Ninja. Machine-specific paths: see tools\build_env.bat.
rem Usage: build_dbg.bat [configure|build]   (default: both)
cd /d "%~dp0"
call "%~dp0tools\build_env.bat" || exit /b 1
if "%1"=="build" goto build
cmake -S . -B build-dbg -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl %PATCHES_FLAGS% || exit /b 1
if "%1"=="configure" exit /b 0
:build
cmake --build build-dbg --target CaliforniaSpeedRecompiled
