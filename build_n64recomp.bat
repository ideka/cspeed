@echo off
rem Build N64Recomp and RSPRecomp (into N64Recomp/build/) with LLVM clang and Ninja.
rem Machine-specific paths: see tools\build_env.bat.
call "%~dp0tools\build_env.bat" || exit /b 1
cd /d "%~dp0N64Recomp" || exit /b 1
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ || exit /b 1
cmake --build build
