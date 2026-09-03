@echo off
REM SquachWatch-Sim -- Windows wrapper.
REM
REM Builds (if needed) and runs the emulator inside WSL, so you can just
REM do `squachsim clear out\clear.png` from a normal Windows prompt
REM instead of hand-writing the wsl/bash/mnt-path incantation.
REM
REM   squachsim clear
REM   squachsim alert out\alert.png --portrait
REM   squachsim clear out\fire.png --bg 6
REM   squachsim --list
setlocal enabledelayedexpansion

if "%~1"=="--list" (
    echo screens: clear log alert settings diary hunt rawscan watchalert colorcheck boot
    echo options: --portrait  --bg N  --theme N  --frames N  --onboard
    exit /b 0
)

REM This script's own directory, translated to a WSL path. %~dp0 ends in
REM a backslash, which would escape the closing quote and hand bash an
REM unterminated string -- so trim it first.
set "SELFDIR=%~dp0"
if "%SELFDIR:~-1%"=="\" set "SELFDIR=%SELFDIR:~0,-1%"
for /f "delims=" %%i in ('wsl wslpath "%SELFDIR%" 2^>nul') do set "SIMDIR=%%i"
if "%SIMDIR%"=="" (
    echo Could not reach WSL. Is it installed? ^(try: wsl --status^)
    exit /b 1
)

REM Default the output path so `squachsim clear` alone does something useful.
set "ARGS=%*"
if "%~2"=="" if not "%~1"=="" set "ARGS=%~1 out\%~1.png"

REM Windows path separators mean nothing to the Linux binary.
set "ARGS=!ARGS:\=/!"

wsl -e bash -lc "cd '%SIMDIR%' && make -s && mkdir -p out && ./squachsim !ARGS!"
