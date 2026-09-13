@echo off
REM ----------------------------------------------------------------------
REM Build usersync with the Vulkan backend (works on NVIDIA / AMD / Intel).
REM Produces:  usersync_vulkan.exe
REM Requires:  Vulkan SDK (VULKAN_SDK env var).
REM            If missing, build.bat will offer to download + install it.
REM ----------------------------------------------------------------------
SET "SCRIPT_DIR=%~dp0"
CALL "%SCRIPT_DIR%build.bat" vulkan %*
EXIT /B %ERRORLEVEL%
