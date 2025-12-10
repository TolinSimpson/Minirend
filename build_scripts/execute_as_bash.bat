@echo off
REM execute_as_bash.bat - Execute a polyglot script using bash
REM
REM Usage: execute_as_bash.bat <script_path> [args...]
REM
REM This script finds a suitable bash interpreter and runs the given script.
REM Supports Git Bash, WSL, MSYS2, and Cygwin.

setlocal enabledelayedexpansion

set "SCRIPT=%~1"
shift

REM Collect remaining arguments
set "ARGS="
:argloop
if "%~1"=="" goto :endargloop
set "ARGS=!ARGS! %1"
shift
goto :argloop
:endargloop

REM Try Git Bash first (most common on Windows)
if exist "C:\Program Files\Git\bin\bash.exe" (
    "C:\Program Files\Git\bin\bash.exe" "%SCRIPT%" %ARGS%
    exit /b %ERRORLEVEL%
)

if exist "C:\Program Files (x86)\Git\bin\bash.exe" (
    "C:\Program Files (x86)\Git\bin\bash.exe" "%SCRIPT%" %ARGS%
    exit /b %ERRORLEVEL%
)

REM Try bash in PATH (MSYS2, Cygwin, or other)
where bash >nul 2>&1
if %ERRORLEVEL% equ 0 (
    bash "%SCRIPT%" %ARGS%
    exit /b %ERRORLEVEL%
)

REM Try WSL
where wsl >nul 2>&1
if %ERRORLEVEL% equ 0 (
    REM Convert Windows path to WSL path
    set "WSL_SCRIPT=%SCRIPT:\=/%"
    set "WSL_SCRIPT=!WSL_SCRIPT:C:=/mnt/c!"
    set "WSL_SCRIPT=!WSL_SCRIPT:D:=/mnt/d!"
    wsl bash "!WSL_SCRIPT!" %ARGS%
    exit /b %ERRORLEVEL%
)

echo ERROR: No bash interpreter found.
echo Please install Git for Windows from https://git-scm.com/download/win
echo or enable Windows Subsystem for Linux (WSL).
exit /b 1


