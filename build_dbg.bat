@echo off
rem Configure + build CaliforniaSpeedRecompiled with the VS 2022 environment, LLVM clang and Ninja.
rem Usage: build.bat [configure|build]   (default: both)
cd /d "%~dp0"
call "%VS_DIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "PATH=%LLVM_DIR%\bin;%VS_DIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%MAKE_BIN%;%PATH%"
if "%1"=="build" goto build
cmake -S . -B build-dbg -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl || exit /b 1
if "%1"=="configure" exit /b 0
:build
cmake --build build-dbg --target CaliforniaSpeedRecompiled
