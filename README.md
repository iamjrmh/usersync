<div align="center">

<img src="https://raw.githubusercontent.com/iamjrmh/CHSuite/refs/heads/main/JURMRWEED.png" alt="usersync" width="120" />

# usersync

**Word-accurate `.lrc` lyric sync for any audio file.**

[![Download](https://img.shields.io/badge/download-latest_release-7C5CFF?style=for-the-badge&logo=download&logoColor=white)](https://github.com/iamjrmh/usersync/releases/latest)
[![Version](https://img.shields.io/badge/version-v1.0-181B25?style=for-the-badge)](https://github.com/iamjrmh/usersync/releases/latest)
[![Windows 10+](https://img.shields.io/badge/Windows-10%2F11-181B25?style=for-the-badge)](https://github.com/iamjrmh/usersync/releases/latest)

[Download](https://github.com/iamjrmh/usersync/releases/latest) - [Install](#install) - [Workflow](#workflow) - [Output format](#output-format) - [Pair with usergram](#pair-with-usergram) - [FAQ](#faq)

</div>

---

Paste your lyrics, point it at a song, get back a precisely-synced enhanced `.lrc` with per-word timestamps. Open it in any LRC viewer or feed it straight into [usergram](https://github.com/iamjrmh/usergram) for animated captions in After Effects.

## Why

Existing whisper.cpp wrappers spit out transcriptions whisper THINKS the lyrics are, which on real music is wrong half the time. usersync flips it: YOU paste the actual lyrics, and the pipeline figures out exactly when each of YOUR words is sung.

Under the hood:

- **Demucs** isolates the vocal stem first - whisper hears the words much more clearly when drums/bass/guitar aren't fighting it.
- **faster-whisper** transcribes the clean vocals with streaming output - you see lines appear live.
- **WhisperX** runs wav2vec2 + CTC forced alignment for sub-50ms per-word timing.
- **Needleman-Wunsch** sequence alignment maps YOUR pasted lyrics onto whisper's now-tight word grid. Your text + whisper's timestamps.
- **Manual editor** for nudging any individual word's start time if the last few ms still bother you.

## Install

1. **[Download the latest release](https://github.com/iamjrmh/usersync/releases/latest)** and unzip it anywhere.
2. Install **Python 3.10 or 3.11** from <https://python.org> (tick **Add python.exe to PATH** during install).
3. Double-click **`setup_whisperx.bat`**. It creates a self-contained `python_env\` next to the exe and installs PyTorch + WhisperX + Demucs (~5 GB, ~10-20 min).
4. Open `Models.txt`, download **`ggml-large-v3.bin`** (~3.1 GB) into the `models\` folder.
5. Double-click **`diagnose.bat`** to verify everything's in place.
6. Launch the exe that matches your hardware:
   - `usersync_cuda.exe` - NVIDIA GPU (fastest)
   - `usersync_vulkan.exe` - AMD / Intel / NVIDIA via Vulkan
   - `usersync_cpu.exe` - no GPU, works anywhere, slow on long songs

The Python env, models, and config persist between launches. Updating to a new release is just unzip + re-launch.

> Want the bare minimum? Skip steps 2-3 - the app runs without Python, just with degraded accuracy (whisper.cpp only, no WhisperX / no vocal isolation). Pasted lyrics still work but timing is sloppier.

## Workflow

1. **Setup** tab - pick the audio file (`mp3` / `wav` / `flac` / `m4a` / `opus`) and an output `.lrc` path.
2. **Lyrics** tab:
   - Check **Use my lyrics (forced alignment)**
   - Check **Use WhisperX (Python, wav2vec2)**
   - Check **Split vocals (Demucs)**
   - Paste the song's lyrics, one line per `.lrc` line
3. Click **GENERATE .LRC** in the footer.
4. Open the **Log** tab to watch it work - Demucs isolating vocals, whisper transcribing line-by-line, per-word matches scrolling by.
5. Optional: **Editor** tab when it's done. **Pull from current job**, nudge any individual word with the +/- 10ms / 50ms buttons, **Save edited .lrc**.

Output works in Lyricify, MusicBee, Salt Player, and any modern LRC viewer that supports enhanced `<mm:ss.xx>` per-word timing - and in [usergram](https://github.com/iamjrmh/usergram) for animated captions.

## Features

| Feature                  | What it does                                                          |
| ------------------------ | --------------------------------------------------------------------- |
| **Forced alignment**     | Your lyrics + whisper's timing = exact text with sub-50ms word sync.  |
| **Vocal isolation**      | Demucs strips the instrumental before transcription. Massive boost.   |
| **Live preview**         | Audio playback with per-word highlighting as the song plays.          |
| **Manual editor**        | Nudge any word's start time in 10ms / 50ms steps.                     |
| **Model dropdown**       | Auto-detects `.bin` files in `models\`, in-app downloader catalog.    |
| **Three GPU backends**   | CUDA / Vulkan / CPU prebuilt - pick whatever your hardware has.       |
| **Streaming log**        | Every step visible in real time. Export the log if anything's off.    |
| **One-click setup**      | `setup_whisperx.bat` does the whole Python env in one go.             |
| **Diagnostic**           | `diagnose.bat` prints exactly what's installed where.                 |

## Output format

Enhanced LRC - `[mm:ss.xx]` per line, `<mm:ss.xx>` per word:

```
[ti:Song Title]
[ar:Artist]
[by:usersync]
[re:usersync (whisperx wav2vec2)]

[00:12.34] <00:12.34>So <00:12.71>tell <00:13.05>me <00:13.40>how <00:14.02>
[00:14.18] <00:14.18>does <00:14.55>it <00:14.81>feel <00:15.22>
```

- `[mm:ss.xx]` line stamps drive the line scroll
- `<mm:ss.xx>` per-word stamps drive the karaoke wipe / per-word fade
- Metadata (`[ti:]`, `[ar:]`, `[al:]`, `[by:]`) preserved
- Unicode and punctuation passed through exactly as you pasted

## Pair with usergram

usersync and [**usergram**](https://github.com/iamjrmh/usergram) are designed to work together:

1. usersync produces the word-timed `.lrc`.
2. usergram is an After Effects panel that consumes the `.lrc` and bakes animated captions (Instagram-style stacked words or classic karaoke wipe) into your comp.

Same audio + lyrics workflow, end-to-end: drop into usersync to time them, drop into usergram to animate them.

## Requirements

- **Windows 10 or 11**, 64-bit
- **Python 3.10 or 3.11** (3.12+ won't work with WhisperX's PyTorch pin)
- **~10 GB free disk** (5 GB for the Python env, 3 GB for the model, 2 GB headroom)
- **8 GB RAM minimum**, 16 GB recommended
- **NVIDIA GPU** with 6+ GB VRAM strongly recommended - CPU mode works but Phase 1 transcription is slow

## FAQ

**Q: It's running on CPU even though I have an NVIDIA GPU.**
Your venv's PyTorch is the CPU build. Re-run `setup_whisperx.bat` - it now pins the CUDA wheels. `diagnose.bat` will confirm `torch.cuda?: True` after.

**Q: `WhisperX not installed` in the Lyrics tab.**
Run `setup_whisperx.bat`, wait for it to finish, then click `recheck` next to the toggle.

**Q: `Compiled backend: CPU only` but I have a GPU.**
You launched `usersync_cpu.exe`. Use `usersync_cuda.exe` (NVIDIA) or `usersync_vulkan.exe` (anything else).

**Q: Demucs vocal isolation crashes with "torchaudio: no appropriate backend".**
Missing `soundfile`. Re-run `setup_whisperx.bat` (newer versions include it).

**Q: Transcription produces 0 segments.**
Re-run `diagnose.bat` to confirm your venv's torch + whisperx + faster_whisper are all importable. If the audio is silent or music-only, that's expected.

**Q: How do I update?**
Download the latest release, unzip over the old folder (or to a new location). Your `python_env\` and `models\` survive an in-place update. Run `diagnose.bat` after to confirm nothing broke.

**Q: Can I distribute the unzipped folder?**
Yes, but skip your `python_env\` and `models\` from the zip (venvs aren't portable across machines, and models are huge). The receiver runs `setup_whisperx.bat` and downloads the model on their side.

**Q: Does it work on macOS / Linux?**
The Python pipeline (`scripts/align.py`) is cross-platform. The C++ GUI app is Windows-only for now.

## License

MIT
