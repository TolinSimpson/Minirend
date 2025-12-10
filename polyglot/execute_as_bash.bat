@echo off
setlocal enabledelayedexpansion

:: Usage: execute_as_bash.bat "<bash_script_to_run>" [args...]
:: Return code: 100 if script to execute was not provided as an argument.
::              101 if script to execute can not be located in the filesystem.
::              102 if bash executeable is not found
:: One caviat. When executing from batch, only double quotes " can be used for grouping of arguments with spaces.
:: All other return codes are comming from the script executed.
:: Rewrite as needed.

if "%~1"=="" (
  echo.[ERROR] %0 did not receive a script to execute. >&2
  exit /b 100
)
if not exist "%~1" (
  echo.[ERROR] %0 can not locate %~1 >&2
  exit /b 101
)

rem ===== Ordered candidate list =====
set "BASH_CANDIDATE[0]=C:\Program Files\Git\bin\bash.exe"
set "BASH_CANDIDATE[1]=C:\Program Files\Git\usr\bin\bash.exe"
set "BASH_CANDIDATE[2]=C:\Program Files\Git\git-bash.exe"
set "BASH_CANDIDATE[3]=C:\msys64\usr\bin\bash.exe"
set "BASH_CANDIDATE[4]=C:\cygwin64\bin\bash.exe"
set BASH_MAX=4

set "BASH_EXE="

:: Search through the candidates. The first found bash, wins.
for /L %%i in (0,1,%BASH_MAX%) do (
  if defined BASH_CANDIDATE[%%i] (
    if exist "!BASH_CANDIDATE[%%i]!" (
      set "BASH_EXE=!BASH_CANDIDATE[%%i]!"
      goto :bash_found
    )
  )
)
echo.[ERROR] %0 did not find a bash. Looked here: >&2
for /L %%i in (0,1,%BASH_MAX%) do if defined BASH_CANDIDATE[%%i] echo.  !BASH_CANDIDATE[%%i]! >&2
exit /b 102

:bash_found
::   %~1  -> the script to run (absolute path from caller)
set "SCRIPT=%~1"
::echo.[NOTICE] "%~f0" is using bash at "!BASH_EXE!" to re-execute "!SCRIPT! in bash mode" >&2

shift
set "ARGS="
:loop
if "%~1"=="" goto run
:: Append the next arg, quoted to preserve spaces
set "ARGS=%ARGS% "%~1""
shift
goto loop

:run
:: Optional hardening: --noprofile --norc to avoid user rc files changing env/cwd
"!BASH_EXE!" -- "!SCRIPT!" %ARGS%
exit /b %ERRORLEVEL%
