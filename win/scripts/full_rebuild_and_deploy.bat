@echo off
rem Regenerate the dictionary DB, rebuild from scratch, run the tests and deploy to win\output.
rem Close Ruigo first. The existing DB is kept if regeneration or verification fails.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -RebuildDictionary -Rebuild -Deploy
exit /b %errorlevel%
