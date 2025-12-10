# Bash/Batch Polyglot Launcher

This gist repo contains a tiny setup that lets a single file (polyglot.bat) behave like:

- a Batch script when run from cmd.exe
- a Bash script when run via bash polyglot.bat (Git Bash/MSYS2/Cygwin)

The appropriate .gitattributes keep  polyglot.bat saved with LF line endings (required for the Bash half), without breaking other .bat files that may need CRLF.

# How is this useful?

Many IDE's allow to specify a post-build action for projects. This is for example true for Eclipse based IDE's.

However, the same IDE's exist for both Windows, Mac and Linux, and when using projects designed on one of these systems, post-build execution usualy break on the others.

For Windows the executed is typically Command.com, for Mac and Linux it's typically bash.

This polyglot file allows to specify one file in the projects and ensures it will execute on all systems.

# Files

 ``` 
polyglot/
├── polyglot.bat          # the actual polyglot script (LF line endings!)
├── execute_as_bash.bat   # Batch helper that finds and invokes bash.exe
└── .gitattributes        # enforces LF for polyglot.bat, CRLF for other .bat
 ``` 

# Quick start

## Windows (cmd.exe):

 ``` polyglot.bat [args...] ``` 


The Batch front calls execute_as_bash.bat, which locates a real Bash and re-executes the same file under Bash.

## Bash (Git Bash/MSYS2/Cygwin):

 ``` bash polyglot.bat [args...] ``` 

Bash skips the Batch block and runs the Bash section directly.


# bash executeables

A list of possible locations to find bash is provided and should be edited as needed. Beware that the bash in Windows32 triggers a WLS bash thats not usefull.


# Example output

From cmd.exe:

 ``` 
c:\Projects\polyglot>polyglot.bat 1 "2 3" 5 '6 7' 8
  Batch: c:\Projects\polyglot\polyglot.bat
  Bash:  /c/Projects/polyglot/polyglot.bat
  $1: 1
  $2: 2 3
  $3: 5
  $4: '6
  $5: 7'
  $6: 8
  $*: 1 2 3 5 '6 7' 8
 ``` 

From Bash:

 ``` 
INTERNAL+DEVELOPMENT+PC MINGW64 /C/Projects/polyglot
$ bash polyglot.bat 1 "2 3" 5 '6 7' 8
  Bash:  /C/Projects/polyglot/polyglot.bat
  $1: 1
  $2: 2 3
  $3: 5
  $4: 6 7
  $5: 8
  $*: 1 2 3 5 6 7 8
 ``` 
 
# Exit codes (rewrite as needed):

100 – no script path provided

101 – script path not found

102 – no suitable bash.exe found

otherwise – the exit code from the Bash code running.


# Notes

Line endings: polyglot.bat must be LF. This repo’s .gitattributes enforces it:

 ``` 
*.bat text eol=crlf
polyglot.bat text eol=lf
 ``` 

If you add the rule later, run:

 ``` 
git add --renormalize polyglot.bat && git commit -m "Normalize polyglot.bat to LF"
 ``` 

Quoting differences: In cmd.exe, only double quotes group arguments; single quotes do not - as shown in the examples.

Unfortunately it's not possible to provide a shebang in the polyglot file.
