@echo off
echo ============================================
echo SeedFinder HTTP Bridge - Build and Start
echo ============================================
echo.

rem Remember the user's Python before the MSYS2 PATH prepend below: ucrt64\bin
rem ships its own python.exe without flask, which would otherwise shadow it and
rem kill step 2 with "No module named flask".
set "PYTHON_EXE="
for /f "delims=" %%P in ('where python 2^>nul') do if not defined PYTHON_EXE set "PYTHON_EXE=%%P"
if not defined PYTHON_EXE (
	echo.
	echo ERROR: python.exe not found on PATH. Install Python 3 and retry.
	pause
	exit /b 1
)

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
echo Python: %PYTHON_EXE%
echo Keep this window open while using SeedFinder.
echo The Minecraft mod will connect to http://localhost:7890
echo.
echo The API server serves on http://localhost:7890
echo.
"%PYTHON_EXE%" -c "import flask, flask_cors" >nul 2>&1
if errorlevel 1 (
	echo.
	echo ERROR: flask/flask-cors missing from the Python above.
	echo Install them with:
	echo.
	echo   "%PYTHON_EXE%" -m pip install -r server\requirements.txt
	echo.
	pause
	exit /b 1
)
"%PYTHON_EXE%" server\index.py --lib-path build_server\seedfinder_lib.dll
pause