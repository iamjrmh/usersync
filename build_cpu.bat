@echo off
REM ----------------------------------------------------------------------
REM Build usersync with the CPU-only backend (no GPU acceleration).
REM Produces:  usersync_cpu.exe
REM Requires:  Nothing extra. Works on every Windows PC.
REM Note:      Slow on large models -- expect minutes per song.
REM ----------------------------------------------------------------------
SET "SCRIPT_DIR=%~dp0"
CALL "%SCRIPT_DIR%build.bat" cpu %*
EXIT /B %ERRORLEVEL%
