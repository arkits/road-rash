@echo off
rem Build and launch the playable game on Windows; see run-windows.ps1.
rem   run-windows.bat [-Debug] [ASSETS_DIR] [rr-game args...]
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-windows.ps1" %*
exit /b %ERRORLEVEL%
