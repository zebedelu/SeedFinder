@echo off
echo ============================================
echo SeedFinder HTTP Bridge - Build and Start
echo ============================================
echo.

set PATH=C:\msys64\mingw64\bin;%PATH%
cd /d "%~dp0\.."

echo [1/2] Building seedfinder_lib.dll...
if not exist build_server mkdir build_server
cd build_server
cmake -G "MinGW Makefiles" -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -S ../core -B . 2>nul
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
python server\seedfinder_server.py --dll-path build_server\seedfinder_lib.dll
pause
