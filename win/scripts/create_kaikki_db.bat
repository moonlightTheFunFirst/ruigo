@echo off
setlocal
rem Build data\kaikki.sqlite from the pinned Kaikki snapshot.
rem The input is downloaded into .wn when missing; tools\import_kaikki.py
rem verifies its SHA-256 and stops without touching the existing DB on mismatch.
set "KAIKKI_FILE=.wn\kaikki-ja-2026-11-01.jsonl.gz"
set "KAIKKI_URL=https://kaikki.org/jawiktionary/raw-wiktextract-data.jsonl.gz"

pushd "%~dp0..\.."
if not exist ".venv\Scripts\python.exe" (
    echo .venv\Scripts\python.exe not found. Create the venv as described in README.md.
    set "RC=1"
    goto end
)
if not exist "%KAIKKI_FILE%" (
    echo %KAIKKI_FILE% not found. Downloading from %KAIKKI_URL% ...
    if not exist ".wn" mkdir ".wn"
    curl.exe -fL --retry 3 -o "%KAIKKI_FILE%.part" "%KAIKKI_URL%"
    if errorlevel 1 (
        echo Download failed.
        if exist "%KAIKKI_FILE%.part" del "%KAIKKI_FILE%.part"
        set "RC=1"
        goto end
    )
    move /y "%KAIKKI_FILE%.part" "%KAIKKI_FILE%" >nul
)
.venv\Scripts\python.exe tools\import_kaikki.py "%KAIKKI_FILE%"
set "RC=%errorlevel%"
if not "%RC%"=="0" (
    echo.
    echo import_kaikki.py failed. If it reports a SHA-256 mismatch, Kaikki has published
    echo a newer dump; delete %KAIKKI_FILE% only after evaluating the new dump as
    echo described in README.md.
)
:end
popd
pause
exit /b %RC%
