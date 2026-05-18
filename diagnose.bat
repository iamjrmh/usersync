@echo off
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
SET "SCRIPT_DIR=%~dp0"
SET "VENV_PY=%SCRIPT_DIR%python_env\Scripts\python.exe"

ECHO ====================================================================
ECHO  usersync diagnostic
ECHO ====================================================================
ECHO.

ECHO [1/6] System Python:
WHERE python 2>&1
python --version 2>&1
ECHO.

ECHO [2/6] Venv Python:
IF NOT EXIST "%VENV_PY%" (
    ECHO   NOT FOUND at %VENV_PY%
    ECHO   ^>^> Run setup_whisperx.bat to create it.
    GOTO :nvidia
)
"%VENV_PY%" --version 2>&1
ECHO.

ECHO [3/6] PyTorch in venv:
"%VENV_PY%" -c "import sys; print(' python:', sys.version)" 2>&1
"%VENV_PY%" -c "import torch; print(' torch  :', torch.__version__); print(' cuda?  :', torch.cuda.is_available()); print(' cuda v :', torch.version.cuda); print(' devices:', torch.cuda.device_count()); print(' gpu    :', torch.cuda.get_device_name(0) if torch.cuda.is_available() else '(none)')" 2>&1
ECHO.

ECHO [4/6] WhisperX in venv:
"%VENV_PY%" -c "import whisperx; print(' whisperx:', getattr(whisperx, '__version__', 'installed'))" 2>&1
ECHO.

ECHO [5/6] faster-whisper in venv:
"%VENV_PY%" -c "import faster_whisper; print(' faster_whisper:', faster_whisper.__version__)" 2>&1
ECHO.

ECHO [6/6] Demucs in venv:
"%VENV_PY%" -c "import demucs; print(' demucs:', demucs.__version__)" 2>&1
ECHO.

:nvidia
ECHO [bonus] NVIDIA driver / nvcc:
nvidia-smi 2>nul | findstr /C:"NVIDIA-SMI" /C:"Driver Version" /C:"CUDA Version"
IF DEFINED CUDA_PATH (
    ECHO   CUDA_PATH = %CUDA_PATH%
) ELSE (
    ECHO   CUDA_PATH not set in this shell.
)
WHERE nvcc 2>NUL
ECHO.

ECHO ====================================================================
ECHO  Done. If anything above shows "NOT FOUND" / "ModuleNotFoundError"
ECHO  or torch.cuda.is_available is False, that's why usersync is slow.
ECHO  Fix:
ECHO    - Missing package        -^> re-run setup_whisperx.bat
ECHO    - torch.cuda is False    -^> Python too new (use 3.11), or no GPU
ECHO ====================================================================
PAUSE
ENDLOCAL
