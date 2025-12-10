:<<BASH_SECTION_START
@echo off
:: ----------------------- bash/batch polyglot file. ------------------------
::  ----------- The batch front: re-executes this file under Bash -----------
::  - Implemented using the here-doc technique, combined with a batch label -
::  --- Command.com requires a batch script to use extension .bat or .cmd ---
::  ------ WARNING - THIS FILE MUST BE SAVES WITH LF LINE ENDINGS ONLY ------
::  -------------------------> See .gitattributes! <-------------------------

echo.  Batch: %~f0
call "%~dp0execute_as_bash.bat" "%~f0" %*
exit /b %ERRORLEVEL%

BASH_SECTION_START
# ----- Bash section (Batch above is skipped) -----
script_full="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)/$(basename -- "${BASH_SOURCE[0]}")"
printf '  Bash:  %s\n' "$script_full"

# Each arg as you'd access it: $1, $2, ...   NOTE $0 may not resolve as expected.
for ((i=1; i<=$#; i++)); do
  printf '  $%d: %s\n' "$i" "${!i}"
done
printf '  $*: %s\n' "$*"

exit 0

