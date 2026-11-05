@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Rebuild -Deploy
exit /b %errorlevel%
