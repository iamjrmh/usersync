@echo off
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
SET "SCRIPT_DIR=%~dp0"
PUSHD "%SCRIPT_DIR%"

REM ----------------------------------------------------------------------
REM usersync build script (lives in /build/, ships into /usersync/)
REM ----------------------------------------------------------------------
REM Layout:
REM   F:\Projects\usersync\
REM     build/        <- THIS script + CMakeLists.txt + src/ + cmake/
REM       _out/       <- CMake configure + compile artifacts (preserved)
REM     usersync/     <- ship folder: exes + scripts/ + helpers + docs/
REM     docs/         <- source of user-facing docs
REM
REM Output: usersync_<backend>.exe lands in ../usersync/  (the ship folder).
REM After build, the script also re-syncs helpers (setup_whisperx.bat,
REM diagnose.bat) and docs/ into /usersync/ so it's always ship-ready.
REM
REM Direct entry points (no prompt):
REM   build_cpu.bat        - CPU only
REM   build_cuda.bat       - NVIDIA CUDA (auto-installs CUDA Toolkit if missing)
REM   build_vulkan.bat     - Vulkan      (auto-installs Vulkan SDK if missing)
REM
REM Or run build.bat directly:
REM   build.bat            - interactive prompt
REM   build.bat cpu/cuda/vulkan
REM   build.bat keep       - skip the artifact wipe (incremental rebuild)
REM ----------------------------------------------------------------------

SET "BUILD_DIR=%SCRIPT_DIR%_out"
SET "SHIP_DIR=%SCRIPT_DIR%..\usersync"
SET "DOCS_DIR=%SCRIPT_DIR%..\docs"
SET "CMAKE_EXTRA="
SET "DO_CLEAN=1"
SET "BACKEND_SET=0"
SET "BACKEND_NAME=CPU"
SET "BACKEND_LOWER=cpu"

REM Installer download URLs (update when newer versions ship).
SET "CUDA_URL=https://developer.download.nvidia.com/compute/cuda/12.6.3/network_installers/cuda_12.6.3_windows_network.exe"
SET "VULKAN_URL=https://sdk.lunarg.com/sdk/download/latest/windows/vulkan_sdk.exe"

:parse_args
IF "%~1"=="" GOTO :args_done
IF /I "%~1"=="keep"   SET "DO_CLEAN=0"
IF /I "%~1"=="clean"  SET "DO_CLEAN=1"
IF /I "%~1"=="cpu"    ( SET "BACKEND_SET=1" & SET "BACKEND_NAME=CPU"    & SET "BACKEND_LOWER=cpu" )
IF /I "%~1"=="cuda"   ( SET "BACKEND_SET=1" & SET "BACKEND_NAME=CUDA"   & SET "BACKEND_LOWER=cuda"   & SET "CMAKE_EXTRA=!CMAKE_EXTRA! -DUSERSYNC_WHISPER_CUDA=ON" )
IF /I "%~1"=="vulkan" ( SET "BACKEND_SET=1" & SET "BACKEND_NAME=Vulkan" & SET "BACKEND_LOWER=vulkan" & SET "CMAKE_EXTRA=!CMAKE_EXTRA! -DUSERSYNC_WHISPER_VULKAN=ON" )
SHIFT
GOTO :parse_args
:args_done

IF "%BACKEND_SET%"=="1" GOTO :backend_done

ECHO.
ECHO ================================================================
ECHO   Which GPU backend should usersync be built with?
ECHO ================================================================
ECHO.
ECHO   [1] CPU only
ECHO       - Works on every PC. No extra install.
ECHO       - SLOW on large models (minutes per song).
ECHO.
ECHO   [2] NVIDIA (CUDA)
ECHO       - For NVIDIA graphics cards ONLY.
ECHO       - Needs the CUDA Toolkit (network installer ~50 MB,
ECHO         full install ~3 GB on disk).
ECHO       - Fastest backend on NVIDIA hardware (5x-20x vs CPU).
ECHO.
ECHO   [3] Other GPU (Vulkan)
ECHO       - Works on NVIDIA, AMD, and Intel GPUs.
ECHO       - Needs the Vulkan SDK (~250 MB).
ECHO       - A bit slower than CUDA but much more portable.
ECHO.
ECHO   If you pick [2] or [3] and don't have the SDK installed,
ECHO   this script will OFFER to download and run the installer
ECHO   for you. (You can always say no and install it manually.)
ECHO.
ECHO ================================================================
CHOICE /C 123 /N /M "Choose [1/2/3]: "
IF ERRORLEVEL 3 GOTO :pick_vulkan
IF ERRORLEVEL 2 GOTO :pick_cuda
GOTO :pick_cpu

:pick_vulkan
SET "BACKEND_NAME=Vulkan"
SET "BACKEND_LOWER=vulkan"
SET "CMAKE_EXTRA=!CMAKE_EXTRA! -DUSERSYNC_WHISPER_VULKAN=ON"
GOTO :backend_done

:pick_cuda
SET "BACKEND_NAME=CUDA"
SET "BACKEND_LOWER=cuda"
SET "CMAKE_EXTRA=!CMAKE_EXTRA! -DUSERSYNC_WHISPER_CUDA=ON"
GOTO :backend_done

:pick_cpu
SET "BACKEND_NAME=CPU"
SET "BACKEND_LOWER=cpu"
GOTO :backend_done

:backend_done
ECHO.
ECHO [usersync] Backend: !BACKEND_NAME!

REM ======================================================================
REM Toolkit detection + auto-install
REM ======================================================================

IF /I NOT "!BACKEND_NAME!"=="CUDA" GOTO :skip_cuda_check
WHERE nvcc >NUL 2>&1
IF NOT ERRORLEVEL 1 (
    ECHO [usersync] CUDA Toolkit detected on PATH. OK.
    GOTO :skip_cuda_check
)

REM nvcc isn't on PATH -- sniff common install locations (CUDA Toolkit
REM installer doesn't always update the current shell's PATH; new shells
REM get it but the one we're in might not).
SET "NVCC_PATH="
IF EXIST "F:\CUDA\bin\nvcc.exe"          SET "NVCC_PATH=F:\CUDA\bin"
IF NOT DEFINED NVCC_PATH IF DEFINED CUDA_PATH    IF EXIST "!CUDA_PATH!\bin\nvcc.exe"   SET "NVCC_PATH=!CUDA_PATH!\bin"
IF NOT DEFINED NVCC_PATH IF DEFINED CUDA_PATH_V12_6 IF EXIST "!CUDA_PATH_V12_6!\bin\nvcc.exe" SET "NVCC_PATH=!CUDA_PATH_V12_6!\bin"
IF NOT DEFINED NVCC_PATH (
    FOR /D %%V IN ("C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v*") DO (
        IF EXIST "%%V\bin\nvcc.exe" SET "NVCC_PATH=%%V\bin"
    )
)
IF NOT DEFINED NVCC_PATH (
    FOR /D %%V IN ("D:\NVIDIA GPU Computing Toolkit\CUDA\v*") DO (
        IF EXIST "%%V\bin\nvcc.exe" SET "NVCC_PATH=%%V\bin"
    )
)
IF DEFINED NVCC_PATH (
    ECHO [usersync] Found nvcc at !NVCC_PATH! -- adding to PATH for this build.
    SET "PATH=!NVCC_PATH!;!PATH!"
    REM Strip "\bin" suffix to get the CUDA install root. MSBuild's CUDA
    REM targets need CUDA_PATH (and version-specific CUDA_PATH_V<v>_<v>)
    REM set, otherwise it produces "CUDA Toolkit v12.6 directory '' does
    REM not exist" mid-build.
    FOR %%I IN ("!NVCC_PATH!\..") DO SET "CUDA_ROOT=%%~fI"
    SET "CUDA_PATH=!CUDA_ROOT!"
    SET "CUDA_PATH_V12_6=!CUDA_ROOT!"
    SET "CUDA_PATH_V12_5=!CUDA_ROOT!"
    SET "CUDA_PATH_V12_4=!CUDA_ROOT!"
    SET "CUDA_PATH_V12_3=!CUDA_ROOT!"
    SET "CUDA_PATH_V12_2=!CUDA_ROOT!"
    SET "CUDA_PATH_V12_1=!CUDA_ROOT!"
    SET "CUDA_PATH_V12_0=!CUDA_ROOT!"
    SET "CudaToolkitDir=!CUDA_ROOT!"
    ECHO [usersync] CUDA_PATH = !CUDA_ROOT!
    GOTO :skip_cuda_check
)
ECHO.
ECHO ================================================================
ECHO   CUDA Toolkit is NOT installed.
ECHO   `nvcc` was not found on your PATH.
ECHO ================================================================
ECHO.
ECHO   What this script will do if you say YES:
ECHO     1. Download NVIDIA's official CUDA network installer
ECHO        (~50 MB, from developer.download.nvidia.com)
ECHO     2. Launch the installer (you'll get a UAC prompt -- accept it)
ECHO     3. The installer downloads + installs ~3 GB of CUDA components
ECHO        (takes 10-20 minutes; you'll see a real installer UI)
ECHO     4. After it finishes, you re-run build.bat from a NEW terminal
ECHO        (so the new CUDA_PATH and PATH entries are picked up)
ECHO.
ECHO   *** IMPORTANT -- HOW TO PICK COMPONENTS ***
ECHO.
ECHO   When the installer opens:
ECHO     1. Pick "Custom (Advanced)" -- NOT "Express"
ECHO     2. UNCHECK the top-level "Driver components" group
ECHO        (this is what would overwrite your gaming driver -- don't!)
ECHO     3. UNCHECK the top-level "Other components" group
ECHO        (PhysX, GeForce Experience -- not needed)
ECHO     4. Under the "CUDA" group, you only NEED:
ECHO          - CUDA -^> Runtime       (cuDART, cuBLAS, etc.)
ECHO          - CUDA -^> Development   (nvcc compiler, headers)
ECHO        Uncheck everything else under CUDA (Documentation,
ECHO        Samples, Nsight, Visual Studio Integration -- not needed
ECHO        for usersync).
ECHO     5. Click Next / Install.
ECHO.
ECHO   *** INSTALL LOCATION ***
ECHO.
ECHO   You can install CUDA to ANY drive (D:\, E:\, etc.) to save C:
ECHO   drive space. The installer sets CUDA_PATH and updates PATH
ECHO   automatically, so usersync will find it wherever you put it.
ECHO   Avoid: network drives, OneDrive/Dropbox folders, removable
ECHO   drives, or paths with non-ASCII characters.
ECHO.
ECHO   With ONLY Runtime + Development selected, install size drops
ECHO   from ~3 GB to ~1 GB and your gaming driver is left alone.
ECHO.
ECHO   If you say NO here, the build is aborted. You can either install
ECHO   CUDA manually from https://developer.nvidia.com/cuda-downloads
ECHO   or re-run `build.bat vulkan` or `build.bat cpu` instead.
ECHO ================================================================
CHOICE /C YN /N /M "Download and run the CUDA installer now? [Y/N]: "
IF ERRORLEVEL 2 GOTO :sdk_install_aborted
SET "INST_PATH=%TEMP%\usersync_cuda_installer.exe"
SET "INST_URL=!CUDA_URL!"
SET "INST_LABEL=CUDA Toolkit"
CALL :download_and_run
IF ERRORLEVEL 1 GOTO :error
GOTO :reopen_terminal_msg
:skip_cuda_check

IF /I NOT "!BACKEND_NAME!"=="Vulkan" GOTO :skip_vulkan_check
IF DEFINED VULKAN_SDK (
    ECHO [usersync] Vulkan SDK detected at !VULKAN_SDK!
    GOTO :skip_vulkan_check
)
ECHO.
ECHO ================================================================
ECHO   Vulkan SDK is NOT installed.
ECHO   The VULKAN_SDK environment variable is not set.
ECHO ================================================================
ECHO.
ECHO   What this script will do if you say YES:
ECHO     1. Download LunarG's official Vulkan SDK installer
ECHO        (~250 MB, from sdk.lunarg.com)
ECHO     2. Launch the installer (you'll get a UAC prompt -- accept it)
ECHO     3. The installer takes about 5-10 minutes
ECHO     4. After it finishes, you re-run build.bat from a NEW terminal
ECHO        (so VULKAN_SDK env var is picked up)
ECHO.
ECHO   If you say NO, the build is aborted. You can either install
ECHO   the SDK manually from https://vulkan.lunarg.com/sdk/home#windows
ECHO   or re-run `build.bat cuda` or `build.bat cpu` instead.
ECHO ================================================================
CHOICE /C YN /N /M "Download and run the Vulkan SDK installer now? [Y/N]: "
IF ERRORLEVEL 2 GOTO :sdk_install_aborted
SET "INST_PATH=%TEMP%\usersync_vulkan_installer.exe"
SET "INST_URL=!VULKAN_URL!"
SET "INST_LABEL=Vulkan SDK"
CALL :download_and_run
IF ERRORLEVEL 1 GOTO :error
GOTO :reopen_terminal_msg
:skip_vulkan_check

REM ======================================================================
REM Actual build
REM ======================================================================

WHERE cmake >NUL 2>&1
IF ERRORLEVEL 1 (
    ECHO [ERROR] cmake is not on PATH. Install CMake 3.21+ and retry. 1>&2
    GOTO :error
)

IF "%DO_CLEAN%"=="1" (
    ECHO [usersync] Cleaning stale build artifacts...
    REM Top-level CMake state
    IF EXIST "%BUILD_DIR%\CMakeCache.txt"   DEL  /F /Q "%BUILD_DIR%\CMakeCache.txt"
    IF EXIST "%BUILD_DIR%\CMakeFiles"       RD   /S /Q "%BUILD_DIR%\CMakeFiles"
    IF EXIST "%BUILD_DIR%\bin"              RD   /S /Q "%BUILD_DIR%\bin"
    IF EXIST "%BUILD_DIR%\Release"          RD   /S /Q "%BUILD_DIR%\Release"
    IF EXIST "%BUILD_DIR%\Debug"            RD   /S /Q "%BUILD_DIR%\Debug"
    FOR %%X IN (vcxproj sln vcxproj.filters) DO (
        DEL /F /Q "%BUILD_DIR%\*.%%X" >NUL 2>&1
    )

    REM CRITICAL: also wipe each dependency's BUILD and SUBBUILD dirs so the
    REM nested whisper/glfw/imgui CMake caches don't keep old backend
    REM settings (e.g. GGML_CUDA=OFF from a previous CPU build). We
    REM intentionally keep *-src/ to avoid re-downloading sources.
    IF EXIST "%BUILD_DIR%\_deps" (
        FOR /D %%D IN ("%BUILD_DIR%\_deps\*-build")    DO RD /S /Q "%%D"
        FOR /D %%D IN ("%BUILD_DIR%\_deps\*-subbuild") DO RD /S /Q "%%D"
    )

    REM Wipe ONLY the current backend's exe in the SHIP folder -- leave
    REM other backends' exes alone so build_all can keep all three.
    IF EXIST "%SHIP_DIR%\usersync_!BACKEND_LOWER!.exe" (
        DEL /F /Q "%SHIP_DIR%\usersync_!BACKEND_LOWER!.exe"
    )
    DEL /F /Q "%SHIP_DIR%\*.dll" >NUL 2>&1
)

IF NOT EXIST "%BUILD_DIR%" MKDIR "%BUILD_DIR%"

ECHO [usersync] Configuring (Release)...
cmake -S . -B _out -DCMAKE_BUILD_TYPE=Release %CMAKE_EXTRA%
IF ERRORLEVEL 1 GOTO :error

ECHO [usersync] Building...
cmake --build _out --config Release --parallel
IF ERRORLEVEL 1 GOTO :error

REM Locate the produced exe and surface it next to this script for convenience.
SET "EXE_SRC="
IF EXIST "%BUILD_DIR%\bin\Release\usersync.exe" SET "EXE_SRC=%BUILD_DIR%\bin\Release\usersync.exe"
IF NOT DEFINED EXE_SRC IF EXIST "%BUILD_DIR%\bin\usersync.exe"          SET "EXE_SRC=%BUILD_DIR%\bin\usersync.exe"
IF NOT DEFINED EXE_SRC IF EXIST "%BUILD_DIR%\Release\usersync.exe"      SET "EXE_SRC=%BUILD_DIR%\Release\usersync.exe"
IF NOT DEFINED EXE_SRC IF EXIST "%BUILD_DIR%\usersync.exe"              SET "EXE_SRC=%BUILD_DIR%\usersync.exe"

IF NOT DEFINED EXE_SRC (
    ECHO [WARN] Build finished but usersync.exe was not found in the usual locations. 1>&2
    GOTO :ok
)

REM Ensure ship folder exists.
IF NOT EXIST "%SHIP_DIR%" MKDIR "%SHIP_DIR%"

SET "DEST_EXE=%SHIP_DIR%\usersync_!BACKEND_LOWER!.exe"
COPY /Y "%EXE_SRC%" "!DEST_EXE!" >NUL

REM Mirror any DLLs sitting next to the freshly-built exe (e.g. CUDA/Vulkan
REM runtime libs) into the ship folder so the standalone exe can launch.
FOR %%D IN ("%EXE_SRC%") DO SET "EXE_DIR=%%~dpD"
IF DEFINED EXE_DIR (
    FOR %%F IN ("%EXE_DIR%*.dll") DO (
        COPY /Y "%%F" "%SHIP_DIR%\" >NUL
    )
)

REM Sync helper scripts into the ship folder so the user has them next to
REM the exe (recreate-python-env / health-check flows).
FOR %%F IN (setup_whisperx.bat diagnose.bat) DO (
    IF EXIST "%SCRIPT_DIR%%%F" COPY /Y "%SCRIPT_DIR%%%F" "%SHIP_DIR%\" >NUL
)

REM Sync user-facing docs (Instructions.md / Models.txt) into the ship folder.
IF EXIST "%DOCS_DIR%" (
    IF NOT EXIST "%SHIP_DIR%\docs" MKDIR "%SHIP_DIR%\docs"
    FOR %%F IN ("%DOCS_DIR%\*.md" "%DOCS_DIR%\*.txt") DO (
        COPY /Y "%%F" "%SHIP_DIR%\docs\" >NUL
    )
    REM Also drop the primary user guide at the ship-folder root so it's
    REM the first thing they see after unzipping.
    IF EXIST "%DOCS_DIR%\Instructions.md" COPY /Y "%DOCS_DIR%\Instructions.md" "%SHIP_DIR%\" >NUL
)

ECHO.
ECHO [usersync] Build OK -^> !DEST_EXE!
ECHO            Ship folder is at: %SHIP_DIR%
ECHO            Double-click usersync_!BACKEND_LOWER!.exe inside it to launch.

:ok
POPD
ENDLOCAL
EXIT /B 0

:error
ECHO [ERROR] Build failed with code %ERRORLEVEL% 1>&2
POPD
ENDLOCAL
EXIT /B %ERRORLEVEL%

REM ======================================================================
REM Subroutines
REM ======================================================================

:sdk_install_aborted
ECHO.
ECHO [usersync] Install aborted. Re-run build.bat when you're ready, or pick
ECHO            a different backend (build.bat cpu / cuda / vulkan).
POPD
ENDLOCAL
EXIT /B 1

:reopen_terminal_msg
ECHO.
ECHO ================================================================
ECHO   Installer has finished (or you closed it).
ECHO.
ECHO   IMPORTANT: PATH and environment variables set by the installer
ECHO   will NOT be visible in this terminal session.
ECHO.
ECHO   To continue:
ECHO     1. Close this terminal.
ECHO     2. Open a NEW terminal.
ECHO     3. cd %SCRIPT_DIR%
ECHO     4. Run build.bat again.
ECHO.
ECHO   If you installed CUDA: verify your gaming driver is still the
ECHO   version you expect. Right-click desktop -^> NVIDIA Control Panel
ECHO   -^> Help -^> System Information. If it got downgraded, just open
ECHO   GeForce Experience and reinstall the latest Game Ready Driver.
ECHO ================================================================
POPD
ENDLOCAL
EXIT /B 0

REM ----------------------------------------------------------------------
REM :download_and_run  --  uses INST_URL, INST_PATH, INST_LABEL
REM Tries curl first, falls back to PowerShell Invoke-WebRequest.
REM ----------------------------------------------------------------------
:download_and_run
ECHO.
ECHO [usersync] Downloading !INST_LABEL! installer...
ECHO            URL : !INST_URL!
ECHO            Dest: !INST_PATH!
IF EXIST "!INST_PATH!" DEL /F /Q "!INST_PATH!" >NUL 2>&1

WHERE curl >NUL 2>&1
IF ERRORLEVEL 1 GOTO :dl_powershell

curl -L --fail --progress-bar -o "!INST_PATH!" "!INST_URL!"
IF ERRORLEVEL 1 (
    ECHO [ERROR] curl download failed. Trying PowerShell as a fallback...
    GOTO :dl_powershell
)
GOTO :dl_done

:dl_powershell
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ProgressPreference='Continue'; try { Invoke-WebRequest -Uri '!INST_URL!' -OutFile '!INST_PATH!' -UseBasicParsing } catch { Write-Host $_; exit 1 }"
IF ERRORLEVEL 1 (
    ECHO [ERROR] Download failed via PowerShell as well.
    ECHO         Check your internet connection or install manually from:
    IF /I "!INST_LABEL!"=="CUDA Toolkit" ECHO           https://developer.nvidia.com/cuda-downloads
    IF /I "!INST_LABEL!"=="Vulkan SDK"   ECHO           https://vulkan.lunarg.com/sdk/home#windows
    EXIT /B 1
)

:dl_done
IF NOT EXIST "!INST_PATH!" (
    ECHO [ERROR] Download reported success but the installer file is missing.
    EXIT /B 1
)

ECHO.
ECHO [usersync] Launching !INST_LABEL! installer...
ECHO            Accept the UAC prompt, then click through the installer UI.
ECHO            This window will resume after the installer closes.
ECHO.
START /WAIT "" "!INST_PATH!"
EXIT /B 0
