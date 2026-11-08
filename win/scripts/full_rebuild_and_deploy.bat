@echo off
rem Regenerate the dictionary DB, rebuild from scratch, run the tests and deploy to win\output.
rem Close Ruigo first. The existing DB is kept if regeneration or verification fails.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -RebuildDictionary -Rebuild -Deploy
if errorlevel 1 exit /b %errorlevel%
rem build.ps1 recreates win\output without the optional Kaikki DB, so add it back when generated.
if exist "%~dp0..\..\data\kaikki.sqlite" copy /y "%~dp0..\..\data\kaikki.sqlite" "%~dp0..\output\data\" >nul
exit /b %errorlevel%
