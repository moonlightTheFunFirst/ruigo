@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Rebuild -Deploy
copy ..\\..\\data\\kaikki.sqlite ..\\output\\data
exit /b %errorlevel%
