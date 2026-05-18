@echo off
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
SET "SCRIPT_DIR=%~dp0"
SET "VENV_DIR=%SCRIPT_DIR%python_env"

ECHO ====================================================================
ECHO   Setting up Python + WhisperX for usersync
ECHO ====================================================================
ECHO.
ECHO   This script creates a self-contained Python venv next to the exe
ECHO   so it can't conflict with anything else on your system.
ECHO.
ECHO   What's about to happen:
ECHO     1. Find Python 3.10+ on PATH
ECHO     2. Create venv at: %VENV_DIR%
ECHO     3. Install PyTorch with CUDA 12.1  (~2.5 GB download)
ECHO     4. Install WhisperX                (~500 MB download)
ECHO     5. Verify everything works
ECHO.
ECHO   Total: ~3 GB / 5-15 minutes (faster on a good connection).
ECHO   Disk:  ~5 GB installed.
ECHO.
ECHO ====================================================================
ECHO.
CHOICE /C YN /N /M "Proceed? [Y/N]: "
IF ERRORLEVEL 2 GOTO :aborted

REM ---- Find Python ----------------------------------------------------
WHERE python >NUL 2>&1
IF ERRORLEVEL 1 (
    ECHO.
    ECHO [ERROR] Python is not on PATH.
    ECHO.
    ECHO   Install Python 3.10 or newer from:
    ECHO     https://www.python.org/downloads/
    ECHO.
    ECHO   IMPORTANT: tick "Add python.exe to PATH" during install.
    ECHO   Then re-run this script from a NEW terminal.
    GOTO :error
)
ECHO.
ECHO [setup] Python found:
python --version
python -c "import sys; ver=sys.version_info; exit(0 if ver >= (3,10) else 1)"
IF ERRORLEVEL 1 (
    ECHO [ERROR] Python 3.10+ required. Found older version above.
    GOTO :error
)

REM ---- Create venv ----------------------------------------------------
IF NOT EXIST "%VENV_DIR%\Scripts\python.exe" (
    ECHO.
    ECHO [setup] Creating venv at %VENV_DIR%...
    python -m venv "%VENV_DIR%"
    IF ERRORLEVEL 1 (
        ECHO [ERROR] venv creation failed.
        GOTO :error
    )
) ELSE (
    ECHO.
    ECHO [setup] Venv already exists at %VENV_DIR% (reusing)
)

SET "VENV_PY=%VENV_DIR%\Scripts\python.exe"
SET "VENV_PIP=%VENV_DIR%\Scripts\pip.exe"

ECHO.
ECHO [setup] Upgrading pip / wheel / setuptools...
"%VENV_PY%" -m pip install --upgrade pip wheel setuptools

REM ---- PyTorch with CUDA ----------------------------------------------
REM Aggressively wipe any existing torch FIRST so we don't end up with a
REM half-CPU half-CUDA install. Then install CUDA wheels. Verify after.
ECHO.
ECHO [setup] Removing any previous PyTorch install in the venv...
"%VENV_PIP%" uninstall -y torch torchvision torchaudio >NUL 2>&1

ECHO.
ECHO [setup] Installing PyTorch 2.8.0 with CUDA 12.6 support...
ECHO         (this download is ~2.5 GB, please be patient)
ECHO         Versions pinned so WhisperX 3.8.x stays compatible.
REM Pin to a torch version WhisperX 3.8.x actually accepts. Without pins,
REM pip may install a too-new torch and whisperx silently doesn't load.
"%VENV_PIP%" install --force-reinstall torch==2.8.0 torchvision==0.23.0 torchaudio==2.8.0 --index-url https://download.pytorch.org/whl/cu126
IF ERRORLEVEL 1 (
    ECHO.
    ECHO [WARN] CUDA PyTorch install failed -- falling back to CPU-only PyTorch.
    ECHO        WhisperX will still work but Phase 1 will be SLOW on songs > 2 min.
    "%VENV_PIP%" install --force-reinstall torch==2.8.0 torchvision==0.23.0 torchaudio==2.8.0
    IF ERRORLEVEL 1 (
        ECHO [ERROR] CPU PyTorch install also failed.
        GOTO :error
    )
)

REM Verify CUDA actually works in torch (not just installed but usable).
ECHO.
ECHO [setup] Verifying PyTorch sees CUDA...
"%VENV_PY%" -c "import torch, sys; ok = torch.cuda.is_available(); print('  torch       :', torch.__version__); print('  built w/CUDA:', torch.version.cuda); print('  torch.cuda  :', ok); sys.exit(0 if ok else 5)"
SET "TORCH_RC=!ERRORLEVEL!"
IF "!TORCH_RC!"=="5" (
    ECHO.
    ECHO [WARN] PyTorch installed but cannot use CUDA.
    ECHO        Possible causes:
    ECHO          - Your Python is too new ^(torch cu121 supports 3.8-3.12 only^)
    ECHO          - Your NVIDIA driver is older than torch needs
    ECHO          - You don't have an NVIDIA GPU
    ECHO        WhisperX will fall back to CPU. Re-run with a supported Python
    ECHO        ^(install Python 3.11 from python.org^) if you want GPU speed.
    ECHO.
    CHOICE /C YN /N /M "Continue with CPU PyTorch anyway? [Y/N]: "
    IF ERRORLEVEL 2 GOTO :error
)

REM ---- WhisperX -------------------------------------------------------
ECHO.
ECHO [setup] Installing WhisperX...
"%VENV_PIP%" install --upgrade whisperx
IF ERRORLEVEL 1 (
    ECHO [ERROR] WhisperX install failed.
    GOTO :error
)

REM ---- Demucs (vocal isolation) + soundfile (torchaudio WAV backend) --
REM torchaudio 2.8 no longer ships its own audio backend by default --
REM `soundfile` is the most reliable WAV write backend across Windows/Linux.
REM Without it, demucs runs but crashes when saving vocals.wav.
ECHO.
ECHO [setup] Installing Demucs + soundfile (for vocal isolation)...
"%VENV_PIP%" install --upgrade demucs soundfile
IF ERRORLEVEL 1 (
    ECHO [WARN] Demucs/soundfile install failed -- usersync will still work,
    ECHO        but the "Split vocals" option won't be available.
)

REM ---- Verify ---------------------------------------------------------
ECHO.
ECHO ====================================================================
ECHO   Verifying...
ECHO ====================================================================
"%VENV_PY%" -c "import torch; print('PyTorch  :', torch.__version__); print('CUDA bld :', torch.version.cuda); print('CUDA OK  :', torch.cuda.is_available()); print('GPU      :', (torch.cuda.get_device_name(0) if torch.cuda.is_available() else '(none)'));"
IF ERRORLEVEL 1 GOTO :error
"%VENV_PY%" -c "import whisperx; print('WhisperX :', getattr(whisperx, '__version__', 'installed'))"
IF ERRORLEVEL 1 GOTO :error

ECHO.
ECHO ====================================================================
ECHO   Setup complete.
ECHO ====================================================================
ECHO.
ECHO   Python venv : %VENV_DIR%
ECHO   Script      : %SCRIPT_DIR%scripts\align.py
ECHO.
ECHO   In usersync_cuda.exe (or your built backend):
ECHO     - Lyrics tab -^> Use my lyrics  (on)
ECHO     - Lyrics tab -^> Use WhisperX   (on)
ECHO     - Hit GENERATE
ECHO.
ECHO   First run downloads the wav2vec2 alignment model (~1 GB).
ECHO   Subsequent runs are fast.
ECHO ====================================================================
ECHO.
PAUSE
ENDLOCAL
EXIT /B 0

:aborted
ECHO.
ECHO Aborted.
PAUSE
ENDLOCAL
EXIT /B 1

:error
ECHO.
ECHO [ERROR] Setup failed. Check the messages above.
PAUSE
ENDLOCAL
EXIT /B 1
