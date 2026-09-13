@echo off
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
SET "SCRIPT_DIR=%~dp0"
SET "SHIP_DIR=%SCRIPT_DIR%..\usersync"
SET "OUT_ZIP=%SCRIPT_DIR%..\usersync.zip"

REM ----------------------------------------------------------------------
REM ship: zip the /usersync/ folder for distribution.
REM
REM /usersync/ is automatically kept ship-ready by build.bat (which copies
REM exes + helpers + docs there). This script just zips the result, minus
REM the parts that don't belong in a release:
REM   - python_env/  (~5 GB, venvs aren't portable across machines)
REM   - docs/       (full guide ships separately on the GitHub release page,
REM                  Instructions.md at the ship-folder root is enough)
REM   - *.ini state (per-run UI state)
REM
REM An EMPTY models/ folder IS included (with a tiny README.txt placeholder
REM so the directory survives the zip) so the receiver has somewhere to
REM drop the .bin file they download.
REM
REM Output:  ../usersync.zip
REM ----------------------------------------------------------------------

IF NOT EXIST "%SHIP_DIR%" (
    ECHO [ERROR] Ship folder does not exist: %SHIP_DIR%
    ECHO         Run build.bat ^(or build_all.bat^) first.
    PAUSE
    EXIT /B 1
)

IF EXIST "%OUT_ZIP%" DEL /F /Q "%OUT_ZIP%"

ECHO ====================================================================
ECHO  Packaging usersync for distribution
ECHO ====================================================================
ECHO   Source : %SHIP_DIR%
ECHO   Output : %OUT_ZIP%
ECHO   Excluded: python_env\, docs\, *.ini
ECHO   Included: an EMPTY models\ folder for the user to drop the .bin into
ECHO ====================================================================
ECHO.

REM Stage in a temp dir so Compress-Archive can include the empty
REM `models/` folder via a README.txt placeholder. Pure batch for
REM staging + one minimal PowerShell call for the actual zip avoids
REM PowerShell quote-escaping landmines.
SET "STAGE=%TEMP%\usersync_ship_%RANDOM%%RANDOM%"
IF EXIST "%STAGE%" RD /S /Q "%STAGE%"
MKDIR "%STAGE%"

ECHO [ship] staging into %STAGE%

REM Copy top-level files (skip *.ini state).
FOR %%F IN ("%SHIP_DIR%\*") DO (
    SET "_ext=%%~xF"
    IF /I NOT "!_ext!"==".ini" (
        COPY /Y "%%F" "%STAGE%\" >NUL
    )
)

REM Copy top-level subfolders, excluding python_env / docs / models.
FOR /D %%D IN ("%SHIP_DIR%\*") DO (
    SET "_name=%%~nxD"
    IF /I NOT "!_name!"=="python_env" IF /I NOT "!_name!"=="docs" IF /I NOT "!_name!"=="models" (
        ECHO [ship]   + !_name!\
        XCOPY /E /I /Y /Q "%%D" "%STAGE%\!_name!\" >NUL
    )
)

REM Empty models/ folder with a placeholder README.txt so the dir
REM survives the zip (Compress-Archive drops empty directories).
MKDIR "%STAGE%\models"
> "%STAGE%\models\README.txt" ECHO Drop ggml-large-v3.bin (or any other ggml-*.bin) into this folder.
>>"%STAGE%\models\README.txt" ECHO.
>>"%STAGE%\models\README.txt" ECHO Direct download:
>>"%STAGE%\models\README.txt" ECHO   https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3.bin
>>"%STAGE%\models\README.txt" ECHO.
>>"%STAGE%\models\README.txt" ECHO See ../Instructions.md for the full setup walkthrough.

ECHO [ship]   + models\ (empty with README.txt placeholder)
ECHO.

ECHO [ship] compressing -^> %OUT_ZIP%
powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -LiteralPath (Get-ChildItem -LiteralPath '%STAGE%' -Force | Select-Object -ExpandProperty FullName) -DestinationPath '%OUT_ZIP%' -CompressionLevel Optimal -Force"
SET "PSRC=%ERRORLEVEL%"

REM Cleanup the staging dir regardless of success/failure.
IF EXIST "%STAGE%" RD /S /Q "%STAGE%"

IF NOT "%PSRC%"=="0" (
    ECHO [ERROR] Compress-Archive failed with exit %PSRC%.
    PAUSE
    EXIT /B 1
)

IF ERRORLEVEL 1 (
    ECHO [ERROR] Compress-Archive failed.
    PAUSE
    EXIT /B 1
)

ECHO.
ECHO ====================================================================
ECHO  Done: %OUT_ZIP%
ECHO ====================================================================
ECHO   Upload this to the GitHub release.
ECHO   First-run setup on the receiver's machine: Instructions.md.
ECHO ====================================================================
PAUSE
ENDLOCAL
EXIT /B 0
