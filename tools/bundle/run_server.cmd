@echo off
rem Starts the umod4 server.  Double-click this file.
cd /d "%~dp0"
where uv >nul 2>nul
if errorlevel 1 (
    echo uv is not installed, or this window cannot find it.
    echo Follow STEP 1 in README.txt, then close this window and try again.
    pause
    exit /b 1
)
uv run --only-group server tools\server\umod4_server.py %*
if errorlevel 1 pause
