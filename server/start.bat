@echo off
echo ============================================
echo SeedFinder HTTP Bridge - Build and Start
echo ============================================
echo.

rem Prepend the MSYS2 UCRT64 64-bit toolchain so gcc produces a 64-bit DLL.
rem (The legacy MinGW.org gcc is 32-bit and its DLL cannot be loaded by a
rem 64-bit Python - WinError 193.)
set PATH=C:\msys64\ucrt64\bin;%PATH%
cd /d "%~dp0\.."

echo [1/2] Building seedfinder_lib.dll...
if not exist build_server mkdir build_server

rem If the cached CMake build was produced by a different compiler, wipe it
rem and rebuild from scratch; otherwise keep the incremental build.
findstr /C:"ucrt64" /C:"mingw64" build_server\CMakeCache.txt >nul 2>&1
if errorlevel 1 goto wipe_stale
goto build

:wipe_stale
echo Compiler changed - wiping stale build cache...
rd /s /q build_server
mkdir build_server

:build
cd build_server
cmake -G "MinGW Makefiles" -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -S ../core -B .
mingw32-make
if %errorlevel% neq 0 (
	echo.
	echo ERROR: Build failed! Check the output above.
	pause
	exit /b 1
)
cd ..

echo.
echo [2/2] Starting SeedFinder server on port 7890...
echo.
echo Keep this window open while using SeedFinder.
echo The Minecraft mod will connect to http://localhost:7890
echo.
echo Serving the same web pages as the Vercel deployment
echo (server\index.py, pages: templates\*, CSS: static\style\*)
echo.
python server\index.py --lib-path build_server\seedfinder_lib.dll
pause