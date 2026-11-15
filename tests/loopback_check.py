#!/usr/bin/env python3
# Dream file loopback checker (used by tests/loopback.sh).
#
#   loopback_check.py tone   <mono|stereo> <input.wav> <decoded.wav> <rx.log>
#   loopback_check.py signal <mono|stereo> <input.wav> <decoded.wav> <rx.log>
#
# tone:   input is 620 Hz (left) / 1130 Hz (right). PASS if audio was decoded
#         (receiver locked) and the steady-state blocks carry the expected
#         tones: left 620 Hz; right 1130 Hz for stereo, 620 Hz for mono.
#         (not periodic in a 40 ms AAC frame: faac codes such a tone badly)
# signal: any input (music, speech). PASS if audio was decoded and its 20 ms
#         envelope follows the source (correlation >= 0.6). Also reports the
#         waveform correlation / SNR after delay alignment (informative only:
#         SBR and PS do not reproduce the waveform).
# Prints one line "PASS|FAIL <details>"; exit status 0 for PASS.
import sys
import wave

try:
    import numpy as np
except ImportError:  # presence check only
    np = None


def read_wav(path):
    w = wave.open(path, 'rb')
    fs, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
    raw = w.readframes(n)
    w.close()
    if np is None:
        return fs, ch, raw
    x = np.frombuffer(raw, '<i2').astype(float).reshape(-1, ch)
    if ch == 1:
        x = np.repeat(x, 2, axis=1)
    return fs, 2, x


def count_errors(log):
    keys = ('other error', 'error parsing', 'DECODER_ERROR', 'fill failed', 'Fill failed')
    try:
        with open(log, errors='replace') as f:
            return sum(1 for line in f if any(k in line for k in keys))
    except OSError:
        return -1


def dominant(x, fs):
    X = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    return np.fft.rfftfreq(len(x), 1.0 / fs)[X.argmax()]


def main():
    kind, expect, src, dec, log = sys.argv[1:6]
    errs = count_errors(log)
    fs, _, y = read_wav(dec)
    if np is None:
        ok = len(y) > fs * 4 * 2
        print('%s decoded=%s errors=%d (numpy missing: presence check only)'
              % ('PASS' if ok else 'FAIL', ok, errs))
        return 0 if ok else 1
    dur = len(y) / fs
    rms = np.sqrt((y ** 2).mean()) if len(y) else 0.0
    if dur < 2.0 or rms < 30:
        print('FAIL no audio decoded (receiver did not lock/decode): %.1f s, rms %.0f, errors %d' % (dur, rms, errs))
        return 1
    if kind == 'tone':
        blk = int(0.4 * fs)
        start = int(1.0 * fs)  # skip the first second (decoder start-up)
        bad, n = 0, 0
        for b in range(start, len(y) - blk + 1, blk):
            s = y[b:b + blk]
            fl, fr = dominant(s[:, 0], fs), dominant(s[:, 1], fs)
            want_r = 1130 if expect == 'stereo' else 620
            n += 1
            if abs(fl - 620) > 15 or abs(fr - want_r) > 15 or np.sqrt((s ** 2).mean()) < 300:
                bad += 1
        ok = n > 0 and bad == 0
        print('%s decoded %.1f s, %d/%d steady 0.4 s blocks with expected tones (L 620 Hz, R %s Hz), decoder error lines %d'
              % ('PASS' if ok else 'FAIL', dur, n - bad, n, '1130' if expect == 'stereo' else '620', errs))
        return 0 if ok else 1
    # signal: align decoded with source (mono mixes) by cross-correlation
    fs_x, _, x = read_wav(src)
    if fs_x != fs:
        print('FAIL sample rate mismatch %d vs %d' % (fs_x, fs))
        return 1
    # skip the first 2 s of decoded audio: start-up (long interleaver still
    # filling, CRC errors; Opus fills them with concealment audio)
    y = y[int(2.0 * fs):]
    xm, ym = x.mean(1), y.mean(1)
    m = 1 << int(np.ceil(np.log2(len(xm) + len(ym))))
    c = np.fft.irfft(np.fft.rfft(ym, m) * np.conj(np.fft.rfft(xm, m)), m)
    k = int(np.argmax(c))
    lag = k if k < len(ym) else k - m  # y[n + lag] ~ x[n]; negative: decoding started later in the source
    x0, y0 = (0, lag) if lag >= 0 else (-lag, 0)
    n = min(len(xm) - x0, len(ym) - y0)
    skip = 0
    a, b = xm[x0 + skip:x0 + n], ym[y0 + skip:y0 + n]
    if len(a) < fs:
        print('FAIL too little overlap after alignment (lag %.2f s)' % (lag / fs))
        return 1
    g = (a @ b) / (a @ a + 1e-9)
    snr = 10 * np.log10((g * a) @ (g * a) / (((b - g * a) ** 2).sum() + 1e-9))
    wcorr = np.corrcoef(a, b)[0, 1]
    e = int(0.02 * fs)
    k = len(a) // e
    ea = np.log10((a[:k * e].reshape(k, e) ** 2).mean(1) + 1)
    eb = np.log10((b[:k * e].reshape(k, e) ** 2).mean(1) + 1)
    ecorr = np.corrcoef(ea, eb)[0, 1]
    sep = ''
    if expect == 'stereo':
        lr = np.corrcoef(y[y0 + skip:y0 + n, 0], y[y0 + skip:y0 + n, 1])[0, 1]
        lrs = np.corrcoef(x[x0 + skip:x0 + n, 0], x[x0 + skip:x0 + n, 1])[0, 1]
        sep = ', L/R corr %.2f (source %.2f)' % (lr, lrs)
    ok = ecorr >= 0.6
    print('%s decoded %.1f s, delay %.2f s, envelope corr %.2f, waveform corr %.2f / SNR %.1f dB%s, decoder error lines %d'
          % ('PASS' if ok else 'FAIL', dur, lag / fs, ecorr, wcorr, snr, sep, errs))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
