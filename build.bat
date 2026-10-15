@echo off
rem Configure + build CaliforniaSpeedRecompiled with the VS 2022 environment, LLVM clang and Ninja.
rem Usage: build.bat [configure|build]   (default: both)
cd /d "%~dp0"
call "%VS_DIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
rem MIPS toolchain for patches/ (mips-elf-gcc built in Cygwin). Cygwin's bin goes at the end of PATH:
rem it is only needed for the toolchain's DLLs and must not shadow the Windows tools.
set "MIPS_BIN=%MIPS_TOOLCHAIN_BIN%"
set "PATCHES_FLAGS="
if exist "%MIPS_BIN%/mips-elf-gcc.exe" set "PATCHES_FLAGS=-DPATCHES_C_COMPILER=%MIPS_BIN%/mips-elf-gcc.exe -DPATCHES_LD=%MIPS_BIN%/mips-elf-ld.exe"
set "PATH=%LLVM_DIR%\bin;%VS_DIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%MAKE_BIN%;%PATH%;%MIPS_TOOLCHAIN_DLL_DIR%"
if "%1"=="build" goto build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl %PATCHES_FLAGS% || exit /b 1
if "%1"=="configure" exit /b 0
:build
cmake --build build --target CaliforniaSpeedRecompiled
