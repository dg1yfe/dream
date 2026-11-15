#!/bin/bash
# Dream end-to-end file loopback: console transmitter -> DRM signal WAV -> console
# receiver -> decoded audio WAV, for several codec configurations. No audio
# hardware involved (transmitter writes the signal with "-O file.wav", the
# receiver reads it with "-f" without real-time pacing).
#
# usage: tests/loopback.sh [audio-file]
#   audio-file            optional extra source (music, speech; any format ffmpeg
#                         or afconvert can read). Also LOOPBACK_INPUT=<file>.
# environment:
#   DREAM_BIN             dream binary (default: build-data/dream, build/dream)
#   LOOPBACK_SECONDS      signal length per case (default 12)
#   LOOPBACK_WORKDIR      keep all files there (default: fresh temp dir)
#   LOOPBACK_CASES        regex selecting cases by name (default: all)
#
# Always runs the tone test (620 Hz left, 1130 Hz right). Without an audio
# file, a speech clip from macOS 'say' is used as second source if available.
# Prints PASS/FAIL per case and a summary; exit status 1 if any case fails.

set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CHECK="$ROOT/tests/loopback_check.py"
SECS=${LOOPBACK_SECONDS:-12}
FILTER=${LOOPBACK_CASES:-.}
INPUT=${1:-${LOOPBACK_INPUT:-}}

DREAM=${DREAM_BIN:-}
if [ -z "$DREAM" ]; then
    for d in "$ROOT/build-data/dream" "$ROOT/build/dream" "$ROOT/build/dream-console" \
             "$ROOT/build/gui/Dream.app/Contents/MacOS/Dream"; do
        [ -x "$d" ] && { DREAM=$d; break; }
    done
fi
[ -x "${DREAM:-}" ] || { echo "dream binary not found (set DREAM_BIN)"; exit 2; }
command -v python3 >/dev/null || { echo "python3 needed"; exit 2; }

WORK=${LOOPBACK_WORKDIR:-$(mktemp -d "${TMPDIR:-/tmp}/dream-loopback.XXXXXX")}
mkdir -p "$WORK"
echo "dream:   $DREAM"
echo "workdir: $WORK"

# run a command with a time limit (no coreutils 'timeout' on macOS)
run_limited() {
    local limit=$1; shift
    "$@" & local pid=$!
    ( sleep "$limit"; kill -9 $pid 2>/dev/null ) & local dog=$!
    wait $pid; local rc=$?
    kill $dog 2>/dev/null; wait $dog 2>/dev/null
    return $rc
}

# --- sources -----------------------------------------------------------------
python3 -I - "$WORK/tone.wav" "$((SECS + 2))" <<'EOF'
import sys, wave, math, struct
path, secs = sys.argv[1], float(sys.argv[2])
fs = 48000
w = wave.open(path, 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(fs)
frames = bytearray()
for i in range(int(fs * secs)):
    t = i / fs
    frames += struct.pack('<hh', int(9830 * math.sin(2 * math.pi * 620 * t)),
                          int(9830 * math.sin(2 * math.pi * 1130 * t)))
w.writeframes(bytes(frames)); w.close()
EOF

# convert any audio file to a 48 kHz stereo 16 bit WAV clip of SECS+1 seconds
to_wav() {
    local in=$1 out=$2
    if command -v ffmpeg >/dev/null; then
        ffmpeg -nostdin -loglevel error -y -t "$((SECS + 1))" -i "$in" -ar 48000 -ac 2 -c:a pcm_s16le "$out"
    elif command -v afconvert >/dev/null; then
        afconvert -f WAVE -d LEI16@48000 -c 2 "$in" "$out"
    else
        return 1
    fi
}

SOURCES="tone:$WORK/tone.wav"
if [ -n "$INPUT" ]; then
    if to_wav "$INPUT" "$WORK/user.wav"; then
        SOURCES="$SOURCES user:$WORK/user.wav"
    else
        echo "cannot convert $INPUT (need ffmpeg or afconvert)"
    fi
elif command -v say >/dev/null; then
    say -o "$WORK/speech.aiff" "Digital Radio Mondiale loopback test. The quick brown fox jumps over the lazy dog. \
One two three four five six seven eight nine ten. This sentence is repeated to fill the clip. \
Digital Radio Mondiale loopback test. The quick brown fox jumps over the lazy dog." 2>/dev/null \
        && to_wav "$WORK/speech.aiff" "$WORK/speech.wav" && SOURCES="$SOURCES speech:$WORK/speech.wav"
fi

# --- cases: name | environment | transmitter options | expected channels ---------
CASES='
fdk-aac-lc-mono    |                       | --codec AAC --aacmode mono                | mono
fdk-aac-lc-stereo  |                       | --codec AAC --aacmode stereo              | stereo
fdk-aac-lc-12k     |                       | --codec AAC --aacmode mono --aacrate 12000 | mono
fdk-aac-sbr        |                       | --codec AAC --aacmode mono --sbr 1        | mono
fdk-aac-sbr-stereo |                       | --codec AAC --aacmode stereo --sbr 1      | stereo
fdk-aac-sbr-ps     |                       | --codec AAC --aacmode pstereo --sbr 1     | stereo
faac-aac-lc-mono   | DREAM_NO_FDK_ENCODER=1 | --codec AAC --aacmode mono                | mono
faac-aac-lc-stereo | DREAM_NO_FDK_ENCODER=1 | --codec AAC --aacmode stereo              | stereo
opus-mono          |                       | --codec Opus --opuschannels OC_MONO       | mono
opus-stereo        |                       | --codec Opus --opuschannels OC_STEREO     | stereo
'

pass=0; fail=0; RESULTS=""
for src in $SOURCES; do
    label=${src%%:*}; wav=${src#*:}
    kind=signal; [ "$label" = tone ] && kind=tone
    while IFS='|' read -r name envs opts expect; do
        name=$(echo $name); envs=$(echo $envs); expect=$(echo $expect)
        [ -z "$name" ] && continue
        echo "$name" | grep -Eq "$FILTER" || continue
        dir="$WORK/$label/$name"; mkdir -p "$dir"
        (
            cd "$dir" || exit 1
            # shellcheck disable=SC2086
            run_limited 300 env $envs "$DREAM" -t -n -f "$wav" -O tx.wav $opts --duration "$SECS" > tx.log 2>&1
            run_limited 300 "$DREAM" -n -f tx.wav -w rx.wav -O audio-out.wav --filepace 0 \
                --duration "$(python3 -c "print($SECS - 0.5)")" > rx.log 2>&1
        )
        res=$(python3 -I "$CHECK" "$kind" "$expect" "$wav" "$dir/rx.wav" "$dir/rx.log" 2>&1)
        if [ "${res%% *}" = PASS ]; then pass=$((pass + 1)); else fail=$((fail + 1)); fi
        line=$(printf '%-7s %-19s %s' "$label" "$name" "$res")
        echo "$line"
        RESULTS="$RESULTS$line"$'\n'
    done <<< "$CASES"
done

echo
echo "summary: $pass passed, $fail failed (files in $WORK)"
[ "$fail" -eq 0 ]
