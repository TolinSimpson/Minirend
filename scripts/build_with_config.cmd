@echo off
REM Minrend build-with-config script (wrapper for unified build_with_config)
REM ------------------------------------------------------------------------
REM This script is a compatibility wrapper that calls the unified build_with_config script.
REM The unified script (scripts/build_with_config) works on both Windows and Unix.
REM
REM Usage:
REM   scripts\build_with_config.cmd [path\to\build.config]

set SCRIPT_DIR=%~dp0
cd /d "%SCRIPT_DIR%.."
set ROOT=%CD%

REM Call the unified build_with_config script (polyglot) via execute_as_bash.bat
call "%ROOT%\polyglot\execute_as_bash.bat" "%ROOT%\scripts\build_with_config" %*
exit /b %ERRORLEVEL%
