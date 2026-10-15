@echo off
cd /d "%~dp0N64Recomp" || exit /b 1
call "%VS_DIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "PATH=%LLVM_DIR%\bin;%VS_DIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ || exit /b 1
cmake --build build
