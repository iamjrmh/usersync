#!/usr/bin/env python3
"""
usersync alignment script.

Pipeline:
    PHASE 0  (optional) Demucs vocal isolation -- splits the song into
             vocals + accompaniment, then aligns against the vocals stem.
             Massively boosts accuracy on music.
    PHASE 1  faster-whisper STREAMING transcription -- yields each segment
             as it's decoded so the UI shows whisper hearing the song live.
    PHASE 2  WhisperX wav2vec2 + CTC forced alignment on whisper's transcript
             -> tight per-word timestamps for what whisper heard.
    PHASE 3  Needleman-Wunsch sequence alignment maps user lyrics onto
             whisper's now-tight word grid -> output text = exactly the
             user's lyrics, timestamps = whisper's tight ones.
    PHASE 4  Write enhanced LRC.

Status output (stderr):
    [PROG] <0-100> <msg>     progress percent + label
    [INFO] / [WARN] / [ERR]  diagnostics
    [SEG]  <t0> <t1> <text>  whisper segment (streams live during PHASE 1)
    [WORD] <t0> <usr> <wsp>  forced-alignment match (streams during PHASE 3)
"""

import argparse
import json
import os
import sys
import time
import traceback
from pathlib import Path


def log(level, msg):
    sys.stderr.write(f"[{level}] {msg}\n")
    sys.stderr.flush()


def progress(pct, msg=""):
    sys.stderr.write(f"[PROG] {pct} {msg}\n")
    sys.stderr.flush()


def seg_event(t0, t1, text):
    text = text.replace("\n", " ").strip()
    if len(text) > 100:
        text = text[:97] + "..."
    sys.stderr.write(f"[SEG] {t0:.2f} {t1:.2f} {text}\n")
    sys.stderr.flush()


def word_event(t, user_word, whisper_word):
    sys.stderr.write(f"[WORD] {t:.3f} {user_word} <-> {whisper_word}\n")
    sys.stderr.flush()


def fmt_ts(t):
    if t < 0:
        t = 0.0
    total_cs = round(t * 100)
    mm = total_cs // 6000
    ss = (total_cs // 100) % 60
    cs = total_cs % 100
    return f"{mm:02d}:{ss:02d}.{cs:02d}"


def normalize(s):
    return "".join(c.lower() for c in s if c.isalnum())


def nw_align(user_words, whisper_words):
    """Needleman-Wunsch. Returns matched whisper index for each user word."""
    N, M = len(user_words), len(whisper_words)
    if N == 0 or M == 0:
        return [-1] * N
    un = [normalize(w) for w in user_words]
    wn = [normalize(w["text"]) for w in whisper_words]
    MATCH, MISMATCH, GAP = 2, -1, -1
    dp = [[0] * (M + 1) for _ in range(N + 1)]
    for i in range(N + 1): dp[i][0] = i * GAP
    for j in range(M + 1): dp[0][j] = j * GAP
    for i in range(1, N + 1):
        for j in range(1, M + 1):
            sc = MATCH if (un[i-1] and un[i-1] == wn[j-1]) else MISMATCH
            dp[i][j] = max(dp[i-1][j-1] + sc, dp[i-1][j] + GAP, dp[i][j-1] + GAP)
    match = [-1] * N
    i, j = N, M
    while i > 0 and j > 0:
        sc = MATCH if (un[i-1] and un[i-1] == wn[j-1]) else MISMATCH
        if dp[i][j] == dp[i-1][j-1] + sc:
            if sc == MATCH:
                match[i-1] = j - 1
            i -= 1; j -= 1
        elif dp[i][j] == dp[i-1][j] + GAP:
            i -= 1
        else:
            j -= 1
    return match


# ---------------------------------------------------------------------------
# PHASE 0: Demucs vocal isolation
# ---------------------------------------------------------------------------
def separate_vocals(audio_path, device):
    """Run Demucs (htdemucs) on audio_path. Saves <name>_vocals.wav next to
    the original audio and returns the new path. Falls back to the original
    audio on any error.

    Uses demucs.separate.main() with CLI args -- this entry point is stable
    across demucs versions (the newer demucs.api Separator class doesn't
    exist in v4.0.x, only in pre-release / dev versions)."""
    progress(5, "PHASE 0 -- Demucs vocal isolation (takes 1-3 min on GPU)")
    log("INFO", "loading Demucs htdemucs (first run downloads ~80 MB)")
    t0 = time.time()

    try:
        import demucs.separate
    except ImportError as e:
        log("WARN", f"demucs not installed ({e}); skipping vocal isolation")
        return audio_path

    src_dir = os.path.dirname(os.path.abspath(audio_path)) or "."
    base    = os.path.splitext(os.path.basename(audio_path))[0]
    # Demucs writes to <out>/<model>/<basename>/<stem>.wav
    out_root  = src_dir
    expected  = os.path.join(out_root, "htdemucs", base, "vocals.wav")

    args = [
        "--two-stems=vocals",
        "-n", "htdemucs",
        "-o", out_root,
        "--device", device,
        audio_path,
    ]
    progress(8, f"PHASE 0 -- separating vocals from {os.path.basename(audio_path)}")
    try:
        demucs.separate.main(args)
    except SystemExit:
        # demucs.separate.main may sys.exit on completion; that's fine.
        pass
    except Exception as e:
        log("WARN", f"demucs separation failed ({e}); falling back to original audio")
        log("WARN", traceback.format_exc())
        return audio_path

    if not os.path.exists(expected):
        log("WARN", f"demucs ran but vocals not found at {expected}; "
                    f"falling back to original audio")
        return audio_path

    # Also drop a friendlier-named copy right next to the source audio so
    # the user can find it.
    try:
        import shutil
        nice_path = os.path.join(src_dir, f"{base}_vocals.wav")
        shutil.copyfile(expected, nice_path)
        log("INFO", f"vocals stem saved to {nice_path}")
        log("INFO", f"PHASE 0 done in {time.time() - t0:.1f}s")
        return nice_path
    except Exception:
        log("INFO", f"vocals stem at {expected}")
        log("INFO", f"PHASE 0 done in {time.time() - t0:.1f}s")
        return expected


# ---------------------------------------------------------------------------
# PHASE 1: streaming transcription via faster-whisper directly
# ---------------------------------------------------------------------------
def transcribe_streaming(audio_path, language, device, compute_type):
    progress(15, "PHASE 1 -- loading faster-whisper large-v2 model")
    log("INFO", "first run downloads ~3 GB of model weights")
    try:
        from faster_whisper import WhisperModel
    except ImportError as e:
        log("ERR ", f"faster-whisper not installed: {e}")
        return None, 0.0

    t0 = time.time()
    model = WhisperModel("large-v2", device=device, compute_type=compute_type)
    log("INFO", f"model loaded in {time.time() - t0:.1f}s")

    progress(20, "PHASE 1 -- transcribing  (segments will appear live below)")
    t0 = time.time()
    # IMPORTANT: vad_filter=False on music. faster-whisper's VAD is tuned
    # for clean speech and will reject most music as "non-speech" -- that's
    # why a previous run showed PHASE 1 finishing in 4s with 0 segments.
    # Without VAD, whisper itself handles silence via no_speech detection.
    segments_iter, info = model.transcribe(
        audio_path,
        language=None if language == "auto" else language,
        beam_size=5,
        vad_filter=False,
        word_timestamps=False,
        no_speech_threshold=0.6,
        condition_on_previous_text=False,
    )
    duration = info.duration or 1.0
    log("INFO", f"audio: {duration:.1f}s  detected language: {info.language} "
                f"(prob {info.language_probability:.2f})")
    log("INFO", f"VAD disabled, beam=5, large-v2 model on {device}/{compute_type}")

    segments = []
    for seg in segments_iter:
        # Each yield = one finished segment. Stream it.
        d = {"start": float(seg.start), "end": float(seg.end), "text": seg.text}
        segments.append(d)
        seg_event(seg.start, seg.end, seg.text)
        pct = 20 + int(40 * (seg.end / duration))
        progress(min(60, pct),
                 f"PHASE 1 -- transcribed {seg.end:.1f}s / {duration:.1f}s  "
                 f"({len(segments)} segments)")

    log("INFO", f"PHASE 1 done in {time.time() - t0:.1f}s ({len(segments)} segments)")

    # Free the ASR model -- alignment uses a separate one.
    del model
    try:
        import torch
        if device == "cuda": torch.cuda.empty_cache()
    except ImportError:
        pass

    return segments, duration


def main():
    p = argparse.ArgumentParser()
    p.add_argument("request")
    p.add_argument("status", nargs="?")
    args = p.parse_args()

    with open(args.request, "r", encoding="utf-8") as f:
        req = json.load(f)

    audio_path    = req["audio"]
    lyrics        = req.get("lyrics", "")
    language      = (req.get("language") or "en").lower()
    if language == "auto":
        language = "en"
    want_gpu      = bool(req.get("use_gpu", True))
    output        = req["output"]
    tags          = req.get("tags", {}) or {}
    split_vocals  = bool(req.get("split_vocals", False))

    progress(1, "loading torch + whisperx")
    try:
        import torch
        import whisperx
    except ImportError as e:
        log("ERR ", f"missing dependency: {e}")
        if args.status:
            Path(args.status).write_text(json.dumps({"ok": False, "error": str(e)}))
        sys.exit(1)

    if want_gpu and torch.cuda.is_available():
        device, compute_type = "cuda", "float16"
        log("INFO", f"device: cuda  ({torch.cuda.get_device_name(0)}, "
                    f"{torch.cuda.get_device_properties(0).total_memory / 1e9:.1f} GB)")
    else:
        device, compute_type = "cpu", "int8"
        if want_gpu:
            log("WARN", "GPU requested but CUDA unavailable -> CPU mode (slow)")

    # ----- PHASE 0: optional Demucs vocal isolation -----------------------
    transcribe_audio_path = audio_path
    if split_vocals:
        transcribe_audio_path = separate_vocals(audio_path, device)

    # ----- PHASE 1: streaming transcription -------------------------------
    segments, duration = transcribe_streaming(
        transcribe_audio_path, language, device, compute_type)
    if not segments:
        log("ERR ", "transcription failed or yielded no segments")
        if args.status:
            Path(args.status).write_text(json.dumps({
                "ok": False, "error": "transcription failed"}))
        sys.exit(1)

    # Load the audio array for alignment (whisperx needs the np array, not a path)
    progress(62, "loading audio for alignment")
    audio = whisperx.load_audio(transcribe_audio_path)

    # ----- PHASE 2: wav2vec2 forced alignment of whisper's transcript -----
    progress(65, "PHASE 2 -- loading wav2vec2 alignment model")
    try:
        align_model, metadata = whisperx.load_align_model(
            language_code=language, device=device)
    except Exception as e:
        log("ERR ", f"load_align_model({language}): {e}")
        if args.status:
            Path(args.status).write_text(json.dumps({"ok": False, "error": str(e)}))
        sys.exit(1)

    progress(70, "PHASE 2 -- CTC forced alignment")
    t0 = time.time()
    try:
        aligned = whisperx.align(
            segments, align_model, metadata, audio, device,
            return_char_alignments=False)
    except Exception as e:
        log("ERR ", f"alignment failed: {e}")
        log("ERR ", traceback.format_exc())
        if args.status:
            Path(args.status).write_text(json.dumps({"ok": False, "error": str(e)}))
        sys.exit(1)
    aligned_segments = aligned["segments"]
    n_aligned_words = sum(len(s.get("words", [])) for s in aligned_segments)
    log("INFO", f"PHASE 2 done in {time.time() - t0:.1f}s  "
                f"({n_aligned_words} timed words)")

    # ----- PHASE 3: map user lyrics onto whisper's grid -------------------
    output_segments = []
    if not lyrics.strip():
        log("INFO", "no user lyrics -- writing whisper's own transcription")
        output_segments = aligned_segments
    else:
        progress(85, "PHASE 3 -- mapping your lyrics onto whisper's word grid")
        whisper_words = []
        for seg in aligned_segments:
            for w in seg.get("words", []):
                if "start" in w and "end" in w and w.get("word", "").strip():
                    whisper_words.append({
                        "text":  w["word"].strip(),
                        "start": w["start"],
                        "end":   w["end"],
                    })
        log("INFO", f"whisper anchors: {len(whisper_words)} words")

        user_lines = []
        for ln in lyrics.splitlines():
            ln = ln.strip()
            if not ln: continue
            user_lines.append(ln.split())
        user_words_flat = [w for ln in user_lines for w in ln]
        log("INFO", f"user lyrics: {len(user_lines)} lines, {len(user_words_flat)} words")

        match = nw_align(user_words_flat, whisper_words)
        n_matched = sum(1 for m in match if m >= 0)
        log("INFO", f"matched {n_matched} / {len(user_words_flat)} user words "
                    f"({100.0 * n_matched / max(1, len(user_words_flat)):.0f}%)")

        for idx, m in enumerate(match):
            if m >= 0 and (idx % 5 == 0 or idx == len(match) - 1):
                word_event(whisper_words[m]["start"], user_words_flat[idx],
                           whisper_words[m]["text"])

        timed = [None] * len(user_words_flat)
        for i, m in enumerate(match):
            if m >= 0:
                timed[i] = (whisper_words[m]["start"], whisper_words[m]["end"])

        prev_anchor = -1
        for i in range(len(timed)):
            if timed[i] is not None:
                if prev_anchor < 0 and i > 0:
                    end_t = timed[i][0]
                    step = max(0.05, end_t / (i + 1))
                    for k in range(i):
                        t = step * k
                        timed[k] = (t, t + step)
                elif prev_anchor >= 0 and i - prev_anchor > 1:
                    a = timed[prev_anchor][1]
                    b = timed[i][0]
                    gap = i - prev_anchor
                    step = max(0.05, (b - a) / gap)
                    for k in range(prev_anchor + 1, i):
                        t = a + step * (k - prev_anchor - 1)
                        timed[k] = (t, t + step)
                prev_anchor = i
        if prev_anchor < 0:
            log("WARN", "no anchor matches -- distributing user lyrics uniformly")
            step = duration / max(1, len(user_words_flat))
            for k in range(len(timed)):
                timed[k] = (k * step, (k + 1) * step)
        elif prev_anchor < len(timed) - 1:
            a = timed[prev_anchor][1]
            rem = len(timed) - 1 - prev_anchor
            step = max(0.05, (duration - a) / (rem + 1))
            for k in range(prev_anchor + 1, len(timed)):
                t = a + step * (k - prev_anchor - 1)
                timed[k] = (t, t + step)

        for k in range(len(timed)):
            t0_, t1_ = timed[k]
            if t1_ < t0_: t1_ = t0_
            if k > 0:
                _, pt1 = timed[k - 1]
                if t0_ < pt1: t0_ = pt1
                if t1_ < t0_: t1_ = t0_
            timed[k] = (t0_, t1_)

        widx = 0
        for line_words in user_lines:
            seg_words = []
            for w_text in line_words:
                t0_, t1_ = timed[widx]
                seg_words.append({"word": w_text, "start": t0_, "end": t1_, "score": 1.0})
                widx += 1
            if seg_words:
                output_segments.append({
                    "start": seg_words[0]["start"],
                    "end":   seg_words[-1]["end"],
                    "text":  " ".join(line_words),
                    "words": seg_words,
                })

    # ----- PHASE 4: write LRC ---------------------------------------------
    progress(97, "writing LRC")
    out_lines = []
    if tags.get("title"):  out_lines.append(f"[ti:{tags['title']}]")
    if tags.get("artist"): out_lines.append(f"[ar:{tags['artist']}]")
    if tags.get("album"):  out_lines.append(f"[al:{tags['album']}]")
    out_lines.append("[by:usersync]")
    out_lines.append("[re:usersync (whisperx wav2vec2)]")
    out_lines.append("")

    n_segments = 0
    n_words    = 0
    for seg in output_segments:
        words = [w for w in seg.get("words", []) if "start" in w and "end" in w]
        if not words: continue
        n_segments += 1
        parts = [f"[{fmt_ts(words[0]['start'])}]"]
        for w in words:
            parts.append(f"<{fmt_ts(w['start'])}>{w.get('word', '').strip()}")
            n_words += 1
        parts.append(f"<{fmt_ts(words[-1]['end'])}>")
        out_lines.append(" ".join(parts))

    Path(output).parent.mkdir(parents=True, exist_ok=True)
    with open(output, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out_lines))
        f.write("\n")
    log("INFO", f"wrote {output}  ({n_segments} segments, {n_words} words)")
    progress(100, "done")

    if args.status:
        Path(args.status).write_text(json.dumps({
            "ok":       True,
            "output":   output,
            "segments": n_segments,
            "words":    n_words,
            "device":   device,
            "vocals":   transcribe_audio_path if split_vocals else None,
        }))


if __name__ == "__main__":
    try:
        main()
    except SystemExit:
        raise
    except Exception as e:
        log("ERR ", f"fatal: {e}")
        log("ERR ", traceback.format_exc())
        sys.exit(2)
