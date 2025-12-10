@echo off
REM Minrend Windows build script (wrapper for unified build)
REM --------------------------------------------------------
REM This script is a compatibility wrapper that calls the unified build script.
REM The unified script (scripts/build) works on both Windows and Unix.
REM
REM Usage:
REM   build_scripts\build.cmd

set SCRIPT_DIR=%~dp0
cd /d "%SCRIPT_DIR%.."
set ROOT=%CD%

REM Call the unified build script (polyglot) via execute_as_bash.bat
call "%ROOT%\build_scripts\execute_as_bash.bat" "%ROOT%\build_scripts\build" %*
exit /b %ERRORLEVEL%
