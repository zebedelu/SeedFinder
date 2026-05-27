@echo off
echo ============================================
echo SeedFinder - Minecraft Structure Scanner
echo ============================================
echo.
echo Starting server on port 7890...
echo The Minecraft mod will connect to http://localhost:7890
echo.
echo Keep this window open while using SeedFinder.
echo Press Ctrl+C to stop.
echo.

"%~dp0dist\SeedFinder.exe" %*
pause
