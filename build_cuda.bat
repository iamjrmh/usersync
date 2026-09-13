@echo off
REM ----------------------------------------------------------------------
REM Build usersync with the NVIDIA CUDA backend.
REM Produces:  usersync_cuda.exe
REM Requires:  NVIDIA CUDA Toolkit (nvcc on PATH).
REM            If missing, build.bat will offer to download + install it.
REM ----------------------------------------------------------------------
SET "SCRIPT_DIR=%~dp0"
CALL "%SCRIPT_DIR%build.bat" cuda %*
EXIT /B %ERRORLEVEL%
