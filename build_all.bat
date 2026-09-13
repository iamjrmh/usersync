@echo off
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
SET "SCRIPT_DIR=%~dp0"
PUSHD "%SCRIPT_DIR%"

REM ----------------------------------------------------------------------
REM Build all three backends in sequence.
REM Produces:  usersync_cpu.exe, usersync_cuda.exe, usersync_vulkan.exe
REM
REM If a backend's SDK is missing (CUDA Toolkit or Vulkan SDK), build.bat
REM will show its install prompt -- answer N to skip that backend; the
REM remaining backends will continue building.
REM ----------------------------------------------------------------------

ECHO ====================================================================
ECHO  Building ALL backends: CPU + CUDA + Vulkan
ECHO ====================================================================
ECHO   Estimated total time:  CPU ~2 min, CUDA ~5-10 min, Vulkan ~3 min
ECHO   Each successful backend lands in ..\usersync\usersync_^<backend^>.exe.
ECHO ====================================================================

SET "SHIP_DIR=%SCRIPT_DIR%..\usersync"
SET "_CPU=missing"
SET "_CUDA=missing"
SET "_VULKAN=missing"

ECHO.
ECHO ==================== [1/3] CPU ====================
CALL "%SCRIPT_DIR%build.bat" cpu
IF EXIST "%SHIP_DIR%\usersync_cpu.exe" SET "_CPU=OK"

ECHO.
ECHO ==================== [2/3] CUDA ====================
CALL "%SCRIPT_DIR%build.bat" cuda
IF EXIST "%SHIP_DIR%\usersync_cuda.exe" SET "_CUDA=OK"

ECHO.
ECHO ==================== [3/3] Vulkan ====================
CALL "%SCRIPT_DIR%build.bat" vulkan
IF EXIST "%SHIP_DIR%\usersync_vulkan.exe" SET "_VULKAN=OK"

ECHO.
ECHO ====================================================================
ECHO  Build summary
ECHO ====================================================================
ECHO   CPU     : !_CPU!    ^(usersync_cpu.exe^)
ECHO   CUDA    : !_CUDA!    ^(usersync_cuda.exe^)
ECHO   Vulkan  : !_VULKAN!    ^(usersync_vulkan.exe^)
ECHO ====================================================================
ECHO   Ship folder: %SHIP_DIR%
ECHO   Recompress + ship.
ECHO ====================================================================
PAUSE
POPD
ENDLOCAL
EXIT /B 0
