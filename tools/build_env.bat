@echo off
rem Sets up the build environment for build.bat, build_dbg.bat and build_n64recomp.bat.
rem
rem Machine-specific settings go in build_env.local.bat at the repository root (git-ignored), e.g.:
rem   set "LLVM_BIN=D:\LLVM\bin"                          (default: %ProgramFiles%\LLVM\bin if it exists)
rem   set "MAKE_BIN=C:\path\to\GnuWin32\bin"              (if GNU make is not already on PATH)
rem   set "MIPS_TOOLCHAIN_BIN=C:\path\to\mips-elf\bin"    (folder with mips-elf-gcc.exe, for patches/)
rem   set "MIPS_TOOLCHAIN_DLL_DIR=C:\path\to\dlls"        (e.g. Cygwin's bin, if the toolchain needs its DLLs)

set "REPO_ROOT=%~dp0.."
if exist "%REPO_ROOT%\build_env.local.bat" call "%REPO_ROOT%\build_env.local.bat"

rem Visual Studio (MSVC environment and its bundled Ninja), located with vswhere.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Visual Studio 2022 with the C++ workload is required ^(vswhere.exe not found^).
    exit /b 1
)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_DIR=%%i"
if not defined VS_DIR (
    echo No Visual Studio installation with the C++ x64 tools was found.
    exit /b 1
)
call "%VS_DIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "PATH=%VS_DIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"

rem LLVM (clang-cl for the game, clang for N64Recomp).
if not defined LLVM_BIN if exist "%ProgramFiles%\LLVM\bin\clang-cl.exe" set "LLVM_BIN=%ProgramFiles%\LLVM\bin"
if defined LLVM_BIN set "PATH=%LLVM_BIN%;%PATH%"

if defined MAKE_BIN set "PATH=%MAKE_BIN%;%PATH%"

rem Optional MIPS toolchain for patches/. Its DLL folder goes at the end of PATH so that it cannot
rem shadow the Windows tools.
set "PATCHES_FLAGS="
if defined MIPS_TOOLCHAIN_BIN set "PATCHES_FLAGS=-DPATCHES_C_COMPILER=%MIPS_TOOLCHAIN_BIN:\=/%/mips-elf-gcc.exe -DPATCHES_LD=%MIPS_TOOLCHAIN_BIN:\=/%/mips-elf-ld.exe"
if defined MIPS_TOOLCHAIN_DLL_DIR set "PATH=%PATH%;%MIPS_TOOLCHAIN_DLL_DIR%"
exit /b 0
