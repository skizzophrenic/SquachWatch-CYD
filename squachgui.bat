@echo off
REM SquachWatch-Sim -- GUI launcher.
REM
REM Builds the emulator, starts the browser GUI inside WSL, and opens it.
REM Leave this window open while you use it; Ctrl-C here stops the server.
REM
REM Windows 10 has no WSLg, so an SDL window would need an X server or a
REM native toolchain. Serving a page the Windows browser opens avoids
REM both -- see gui.py.
setlocal

REM %~dp0 ends in a backslash, which would escape the closing quote and
REM hand bash an unterminated string, so trim it before wslpath sees it.
set "SELFDIR=%~dp0"
if "%SELFDIR:~-1%"=="\" set "SELFDIR=%SELFDIR:~0,-1%"

for /f "delims=" %%i in ('wsl wslpath "%SELFDIR%" 2^>nul') do set "SIMDIR=%%i"
if "%SIMDIR%"=="" (
    echo Could not reach WSL. Is it installed? ^(try: wsl --status^)
    exit /b 1
)

if "%SQUACHSIM_PORT%"=="" set "SQUACHSIM_PORT=842"

echo Building...
wsl -e bash -lc "cd '%SIMDIR%' && make -s"
if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

REM Open the browser a moment after the server starts. Launched detached
REM so the server can hold this window in the foreground, where Ctrl-C
REM reaches it.
start "" cmd /c "timeout /t 2 >nul & start "" http://localhost:%SQUACHSIM_PORT%/"

echo.
echo   GUI at http://localhost:%SQUACHSIM_PORT%/   (Ctrl-C to stop)
echo.
wsl -e bash -lc "cd '%SIMDIR%' && SQUACHSIM_PORT=%SQUACHSIM_PORT% python3 gui.py"
