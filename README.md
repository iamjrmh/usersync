# usersync - User Guide

Word-accurate lyric sync. Paste your lyrics, point it at the audio file,
get back a precisely-timed `.lrc` with per-word timestamps.

> **This guide is for using the prebuilt app.** Nothing here compiles
> anything - you've got the `.exe` already, this just walks you through
> first-time setup and how to drive it.

---

## 0. Download usersync

Grab the latest release from:

<https://github.com/iamjrmh/usersync/releases/latest>

Unzip anywhere you like. The rest of this guide assumes you're working
inside that unzipped folder.

---

## 1. Install Python 3.10 or 3.11

Get it from <https://www.python.org/downloads/>. During install, **check
the "Add python.exe to PATH" box** at the bottom of the first screen.

Confirm in a terminal:
```bat
python --version
```
Should print `Python 3.10.x` or `Python 3.11.x`. (3.12+ won't work with
WhisperX's PyTorch pin.)

## 2. Run `setup_whisperx.bat`

Just double-click it. It downloads and installs everything WhisperX
needs into a self-contained `python_env\` folder right next to the
`.exe`:

- PyTorch 2.8.0 (CUDA 12.6 for NVIDIA GPUs)
- WhisperX, faster-whisper, Demucs, soundfile

~5 GB download, ~10-20 min depending on your connection.

## 3. Download a model

Open `Models.txt` for the full catalog. The one you want:

**`ggml-large-v3.bin`** (~3.1 GB)

Direct link:
<https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3.bin>

Save it into the `models\` folder next to the `.exe`.

## 4. Verify everything

Double-click `diagnose.bat`. You should see:

- `torch.cuda?: True` (if you have an NVIDIA GPU)
- `whisperx`, `faster_whisper`, `demucs`, `soundfile` all OK
- `models\ggml-large-v3.bin` present

If anything's red/missing, re-run `setup_whisperx.bat` or re-download
the model. Then you're ready to launch.

---

## Pick your exe

| Exe | When |
|---|---|
| `usersync_cuda.exe` | NVIDIA GPU - fastest |
| `usersync_vulkan.exe` | AMD / Intel / NVIDIA via Vulkan |
| `usersync_cpu.exe` | No GPU - works anywhere, slow on long songs |

Double-click to launch.

---

## Using it

1. **Setup** tab - pick the audio file and an output `.lrc` path.
2. **Lyrics** tab:
   - Check **Use my lyrics (forced alignment)**
   - Check **Use WhisperX (Python, wav2vec2)**
   - Check **Split vocals (Demucs)**
   - Paste the song's lyrics, one line per `.lrc` line
3. Click **GENERATE .LRC** in the footer.
4. Watch the **Log** tab - Demucs isolates vocals, whisper transcribes
   the clean vocals line by line, then per-word forced alignment runs.
5. Optional: open the **Editor** tab when it's done, click **Pull from
   current job**, nudge any individual word with the +/- 10ms / 50ms
   buttons, then **Save edited .lrc**.

Output works in Lyricify, MusicBee, Salt Player, and any modern LRC
viewer that supports enhanced `<mm:ss.xx>` per-word timing.

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| `WhisperX not installed` in the Lyrics tab | Run `setup_whisperx.bat`, then click `recheck` |
| `Compiled backend: CPU only` and you have a GPU | You launched `usersync_cpu.exe` - use `usersync_cuda.exe` or `_vulkan.exe` instead |
| Transcription is slow on a fast GPU | Your venv's PyTorch is the CPU build. Re-run `setup_whisperx.bat` - it now pins the CUDA wheels |
| `demucs not installed` or `torchaudio: no backend` | Re-run `setup_whisperx.bat` (older versions skipped these) |
| Anything weird | Open the **Log** tab, click **Export...** to save the full log to a `.txt`, then read it. `diagnose.bat` also dumps your full environment state |

---

## Output format

Enhanced LRC - `[mm:ss.xx]` per line, `<mm:ss.xx>` per word.

```
[ti:Song Title]
[ar:Artist]
[by:usersync]

[00:12.34] <00:12.34>So <00:12.71>tell <00:13.05>me <00:13.40>how <00:14.02>
[00:14.18] <00:14.18>does <00:14.55>it <00:14.81>feel <00:15.22>
```
