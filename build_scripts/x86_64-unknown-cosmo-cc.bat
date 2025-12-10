@echo off
setlocal enabledelayedexpansion
REM Wrapper for Cosmopolitan C compiler on Windows
REM Invokes cosmocc through bash since it's a shell script

set "SCRIPT_DIR=%~dp0"
set "ROOT=%SCRIPT_DIR%.."
set "COSMO_BIN=%ROOT%\cosmocc\bin"
set "COSMOCC=%COSMO_BIN%\cosmocc"

REM Find bash - prefer Git Bash
set "BASH_EXE="
if exist "C:\Program Files\Git\bin\bash.exe" set "BASH_EXE=C:\Program Files\Git\bin\bash.exe"
if "!BASH_EXE!"=="" if exist "C:\Program Files\Git\usr\bin\bash.exe" set "BASH_EXE=C:\Program Files\Git\usr\bin\bash.exe"
if "!BASH_EXE!"=="" if exist "C:\msys64\usr\bin\bash.exe" set "BASH_EXE=C:\msys64\usr\bin\bash.exe"
if "!BASH_EXE!"=="" if exist "C:\cygwin64\bin\bash.exe" set "BASH_EXE=C:\cygwin64\bin\bash.exe"

if "!BASH_EXE!"=="" (
    echo [ERROR] Could not find bash.exe >&2
    exit /b 1
)

REM Run cosmocc directly through bash, passing all arguments
"!BASH_EXE!" "%COSMOCC%" %*
exit /b %ERRORLEVEL%
