@echo off
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
SET "SCRIPT_DIR=%~dp0"
PUSHD "%SCRIPT_DIR%"

REM ----------------------------------------------------------------------
REM usersync-forked build script (lives in /build/, ships to the repo root)
REM ----------------------------------------------------------------------
REM No whisper.cpp, no GPU backend, no Python -- just glfw + imgui + miniaudio,
REM so this is a plain single-config CMake build.
REM
REM   build.bat          - incremental build
REM   build.bat clean     - wipe CMake cache + rebuild from scratch
REM ----------------------------------------------------------------------

SET "BUILD_DIR=%SCRIPT_DIR%_out"
SET "SHIP_DIR=%SCRIPT_DIR%.."

IF /I "%~1"=="clean" (
    ECHO [usersync-forked] Cleaning stale build artifacts...
    IF EXIST "%BUILD_DIR%" RD /S /Q "%BUILD_DIR%"
)

WHERE cmake >NUL 2>&1
IF ERRORLEVEL 1 (
    ECHO [ERROR] cmake is not on PATH. Install CMake 3.21+ and retry. 1>&2
    GOTO :error
)

IF NOT EXIST "%BUILD_DIR%" MKDIR "%BUILD_DIR%"

ECHO [usersync-forked] Configuring (Release)...
cmake -S . -B _out -DCMAKE_BUILD_TYPE=Release
IF ERRORLEVEL 1 GOTO :error

ECHO [usersync-forked] Building...
cmake --build _out --config Release --parallel
IF ERRORLEVEL 1 GOTO :error

SET "EXE_SRC="
IF EXIST "%BUILD_DIR%\bin\Release\usersync-forked.exe" SET "EXE_SRC=%BUILD_DIR%\bin\Release\usersync-forked.exe"
IF NOT DEFINED EXE_SRC IF EXIST "%BUILD_DIR%\bin\usersync-forked.exe"     SET "EXE_SRC=%BUILD_DIR%\bin\usersync-forked.exe"
IF NOT DEFINED EXE_SRC IF EXIST "%BUILD_DIR%\Release\usersync-forked.exe" SET "EXE_SRC=%BUILD_DIR%\Release\usersync-forked.exe"
IF NOT DEFINED EXE_SRC IF EXIST "%BUILD_DIR%\usersync-forked.exe"        SET "EXE_SRC=%BUILD_DIR%\usersync-forked.exe"

IF NOT DEFINED EXE_SRC (
    ECHO [WARN] Build finished but usersync-forked.exe was not found in the usual locations. 1>&2
    GOTO :ok
)

IF NOT EXIST "%SHIP_DIR%" MKDIR "%SHIP_DIR%"
COPY /Y "%EXE_SRC%" "%SHIP_DIR%\usersync-forked.exe" >NUL

FOR %%D IN ("%EXE_SRC%") DO SET "EXE_DIR=%%~dpD"
IF DEFINED EXE_DIR (
    FOR %%F IN ("%EXE_DIR%*.dll") DO (
        COPY /Y "%%F" "%SHIP_DIR%\" >NUL
    )
)

ECHO.
ECHO [usersync-forked] Build OK -^> %SHIP_DIR%\usersync-forked.exe
ECHO                    Double-click it to launch.

:ok
POPD
ENDLOCAL
EXIT /B 0

:error
ECHO [ERROR] Build failed with code %ERRORLEVEL% 1>&2
POPD
ENDLOCAL
EXIT /B %ERRORLEVEL%
