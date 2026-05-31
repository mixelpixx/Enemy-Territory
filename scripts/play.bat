@echo off
rem ---------------------------------------------------------------------------
rem  Launch the ET-RM client (boots to the main menu).
rem  Runs from build\bin so the game modules (*_mp_x86_64.dll) are found, and
rem  points fs_basepath at your retail ET game data (the folder containing
rem  etmain\pak0.pk3 etc.). Edit ET_BASEPATH below if your install moves.
rem
rem  Extra args pass through, e.g.:  play.bat +devmap oasis
rem ---------------------------------------------------------------------------
setlocal
set "BIN=%~dp0..\build\bin"
if "%ET_BASEPATH%"=="" set "ET_BASEPATH=C:\repo\enemy-territory-RM"

if not exist "%BIN%\etrm.exe" (
    echo etrm.exe not found in "%BIN%".
    echo Build first:  cmake --build build
    pause
    exit /b 1
)

cd /d "%BIN%"
"%BIN%\etrm.exe" +set fs_basepath "%ET_BASEPATH%" +set fs_homepath "%BIN%" +set sv_pure 0 +set r_fullscreen 0 %*
endlocal
