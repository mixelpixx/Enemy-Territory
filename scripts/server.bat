@echo off
rem ---------------------------------------------------------------------------
rem  Launch the ET-RM dedicated server on a map (default: oasis).
rem  Runs from build\bin so the game module (qagame_mp_x86_64.dll) is found.
rem  Edit ET_BASEPATH below if your install moves.
rem
rem  Override the map, e.g.:  server.bat +map radar
rem  Then connect a client:   play.bat +connect 127.0.0.1
rem ---------------------------------------------------------------------------
setlocal
set "BIN=%~dp0..\build\bin"
if "%ET_BASEPATH%"=="" set "ET_BASEPATH=C:\repo\enemy-territory-RM"

if not exist "%BIN%\etrmded.exe" (
    echo etrmded.exe not found in "%BIN%".
    echo Build first:  cmake --build build
    pause
    exit /b 1
)

cd /d "%BIN%"
if "%~1"=="" (
    "%BIN%\etrmded.exe" +set dedicated 1 +set fs_basepath "%ET_BASEPATH%" +set fs_homepath "%BIN%" +set sv_pure 0 +map oasis
) else (
    "%BIN%\etrmded.exe" +set dedicated 1 +set fs_basepath "%ET_BASEPATH%" +set fs_homepath "%BIN%" +set sv_pure 0 %*
)
endlocal
