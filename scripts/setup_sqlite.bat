@echo off
setlocal

set SQLITE_URL=https://www.sqlite.org/2025/sqlite-amalgamation-3490100.zip
set DEST_DIR=%~dp0..\project\application\externals\sqlite3
set TEMP_ZIP=%TEMP%\sqlite3.zip

echo === SQLite3 Setup ===

if exist "%DEST_DIR%\sqlite3.h" (
    echo Already exists: %DEST_DIR%\sqlite3.h
    echo Delete the folder to re-download.
    pause
    exit /b 0
)

echo Downloading SQLite amalgamation...
curl -L -o "%TEMP_ZIP%" "%SQLITE_URL%"
if errorlevel 1 (
    echo Download failed.
    pause
    exit /b 1
)

echo Extracting...
mkdir "%DEST_DIR%" 2>nul
powershell -Command "Expand-Archive -Path '%TEMP_ZIP%' -DestinationPath '%TEMP%\sqlite3_tmp' -Force"

for /d %%D in ("%TEMP%\sqlite3_tmp\sqlite-amalgamation-*") do (
    copy "%%D\sqlite3.h" "%DEST_DIR%\" >nul
    copy "%%D\sqlite3.c" "%DEST_DIR%\" >nul
    copy "%%D\sqlite3ext.h" "%DEST_DIR%\" >nul
)

rd /s /q "%TEMP%\sqlite3_tmp" 2>nul
del "%TEMP_ZIP%" 2>nul

if exist "%DEST_DIR%\sqlite3.h" (
    echo Done: %DEST_DIR%
) else (
    echo Failed to extract sqlite3 files.
    pause
    exit /b 1
)

pause
