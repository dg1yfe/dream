/******************************************************************************\
 * Codec independent packet loss concealment on the decoded PCM signal
 *
 * The pitch search, the LPC analysis/filters and the periodic extrapolation
 * below are a C++ port of the float code paths of the CELT decoder of
 * Opus 1.6.1 (celt/pitch.c: pitch_downsample, pitch_search, find_best_pitch;
 * celt/celt_lpc.c: _celt_lpc, celt_fir, celt_iir, _celt_autocorr;
 * celt/celt_decoder.c: celt_plc_pitch_search, celt_decode_lost), which are
 * distributed under the following licence:
 *
 *   Copyright (c) 2007-2008 CSIRO
 *   Copyright (c) 2007-2010 Xiph.Org Foundation
 *   Copyright (c) 2008 Gregory Maxwell
 *   Written by Jean-Marc Valin and Gregory Maxwell
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *   - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 *   - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 *   OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 *   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 *   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Adaptation to DREAM (any sample rate, frames of any length, history of the
 * output signal, LPC shaped noise instead of CELT's band noise, cross-fades
 * instead of the MDCT overlap): DREAM project, GPL v2 or later.
\******************************************************************************/

#include "concealment.h"
#include <cmath>
#include <algorithm>
#include <cstdlib>

static const int LPC_ORDER = 24; /* CELT_LPC_ORDER */

/* ---- celt_lpc.c (float) ------------------------------------------------- */

static void celt_lpc(_REAL* lpc, const _REAL* ac, int p)
{
    _REAL error = ac[0];
    for (int i = 0; i < p; i++)
        lpc[i] = 0;
    if (ac[0] > 1e-10)
    {
        for (int i = 0; i < p; i++)
        {
            /* Sum up this iteration's reflection coefficient */
            _REAL rr = 0;
            for (int j = 0; j < i; j++)
                rr += lpc[j] * ac[i - j];
            rr += ac[i + 1];
            const _REAL r = -rr / error;
            /* Update LPC coefficients and total error */
            lpc[i] = r;
            for (int j = 0; j < (i + 1) >> 1; j++)
            {
                const _REAL tmp1 = lpc[j];
                const _REAL tmp2 = lpc[i - 1 - j];
                lpc[j] = tmp1 + r * tmp2;
                lpc[i - 1 - j] = tmp2 + r * tmp1;
            }
            error = error - r * r * error;
            /* Bail out once we get 30 dB gain */
            if (error <= .001 * ac[0])
                break;
        }
    }
}

/* y[i] = x[i] + sum_k num[k] x[i-k-1], x[-ord..-1] must be valid */
static void celt_fir(const _REAL* x, const _REAL* num, _REAL* y, int N, int ord)
{
    for (int i = 0; i < N; i++)
    {
        _REAL sum = x[i];
        for (int k = 0; k < ord; k++)
            sum += num[k] * x[i - k - 1];
        y[i] = sum;
    }
}

/* y[i] = x[i] - sum_k den[k] y[i-k-1], mem[k] = y[-k-1]; in place allowed */
static void celt_iir(const _REAL* x, const _REAL* den, _REAL* y, int N, int ord, _REAL* mem)
{
    std::vector<_REAL> m(mem, mem + ord);
    for (int i = 0; i < N; i++)
    {
        _REAL sum = x[i];
        for (int k = 0; k < ord; k++)
            sum -= den[k] * m[k];
        for (int k = ord - 1; k >= 1; k--)
            m[k] = m[k - 1];
        m[0] = sum;
        y[i] = sum;
    }
    for (int k = 0; k < ord; k++)
        mem[k] = m[k];
}

static void celt_autocorr(const _REAL* x, _REAL* ac, const _REAL* window,
                          int overlap, int lag, int n)
{
    std::vector<_REAL> xx(x, x + n);
    for (int i = 0; i < overlap; i++)
    {
        xx[i] *= window[i];
        xx[n - i - 1] *= window[i];
    }
    for (int k = 0; k <= lag; k++)
    {
        _REAL d = 0;
        for (int i = k; i < n; i++)
            d += xx[i] * xx[i - k];
        ac[k] = d;
    }
}

/* ---- pitch.c (float) ---------------------------------------------------- */

static void find_best_pitch(const _REAL* xcorr, const _REAL* y, int len,
                            int max_pitch, int* best_pitch)
{
    _REAL Syy = 1;
    _REAL best_num[2] = {-1, -1};
    _REAL best_den[2] = {0, 0};
    best_pitch[0] = 0;
    best_pitch[1] = 1;
    for (int j = 0; j < len; j++)
        Syy += y[j] * y[j];
    for (int i = 0; i < max_pitch; i++)
    {
        if (xcorr[i] > 0)
        {
            const _REAL xcorr16 = xcorr[i] * 1e-12;
            const _REAL num = xcorr16 * xcorr16;
            if (num * best_den[1] > best_num[1] * Syy)
            {
                if (num * best_den[0] > best_num[0] * Syy)
                {
                    best_num[1] = best_num[0];
                    best_den[1] = best_den[0];
                    best_pitch[1] = best_pitch[0];
                    best_num[0] = num;
                    best_den[0] = Syy;
                    best_pitch[0] = i;
                }
                else
                {
                    best_num[1] = num;
                    best_den[1] = Syy;
                    best_pitch[1] = i;
                }
            }
        }
        Syy += y[i + len] * y[i + len] - y[i] * y[i];
        Syy = std::max(_REAL(1), Syy);
    }
}

static void pitch_downsample(const _REAL* const x[2], _REAL* x_lp, int len, int C, int factor)
{
    const int offset = factor / 2;
    for (int i = 1; i < len; i++)
        x_lp[i] = .25 * x[0][factor * i - offset] + .25 * x[0][factor * i + offset] + .5 * x[0][factor * i];
    x_lp[0] = .25 * x[0][offset] + .5 * x[0][0];
    if (C == 2)
    {
        for (int i = 1; i < len; i++)
            x_lp[i] += .25 * x[1][factor * i - offset] + .25 * x[1][factor * i + offset] + .5 * x[1][factor * i];
        x_lp[0] += .25 * x[1][offset] + .5 * x[1][0];
    }
    _REAL ac[5];
    celt_autocorr(x_lp, ac, nullptr, 0, 4, len);
    /* Noise floor -40 dB */
    ac[0] *= 1.0001;
    /* Lag windowing */
    for (int i = 1; i <= 4; i++)
        ac[i] -= ac[i] * (.008 * i) * (.008 * i);
    _REAL lpc[4];
    celt_lpc(lpc, ac, 4);
    _REAL tmp = 1.0;
    for (int i = 0; i < 4; i++)
    {
        tmp *= .9;
        lpc[i] *= tmp;
    }
    /* Add a zero */
    const _REAL c1 = .8;
    _REAL lpc2[5];
    lpc2[0] = lpc[0] + .8;
    lpc2[1] = lpc[1] + c1 * lpc[0];
    lpc2[2] = lpc[2] + c1 * lpc[1];
    lpc2[3] = lpc[3] + c1 * lpc[2];
    lpc2[4] = c1 * lpc[3];
    /* celt_fir5, in place */
    _REAL mem[5] = {0, 0, 0, 0, 0};
    for (int i = 0; i < len; i++)
    {
        const _REAL sum = x_lp[i] + lpc2[0] * mem[0] + lpc2[1] * mem[1]
                + lpc2[2] * mem[2] + lpc2[3] * mem[3] + lpc2[4] * mem[4];
        mem[4] = mem[3];
        mem[3] = mem[2];
        mem[2] = mem[1];
        mem[1] = mem[0];
        mem[0] = x_lp[i];
        x_lp[i] = sum;
    }
}

static void pitch_xcorr(const _REAL* x, const _REAL* y, _REAL* xcorr, int len, int max_pitch)
{
    for (int i = 0; i < max_pitch; i++)
    {
        _REAL sum = 0;
        for (int j = 0; j < len; j++)
            sum += x[j] * y[i + j];
        xcorr[i] = sum;
    }
}

static void pitch_search(const _REAL* x_lp, const _REAL* y, int len, int max_pitch, int* pitch)
{
    int best_pitch[2] = {0, 0};
    const int lag = len + max_pitch;
    std::vector<_REAL> x_lp4(len >> 2), y_lp4(lag >> 2), xcorr(max_pitch >> 1);

    /* Downsample by 2 again */
    for (int j = 0; j < len >> 2; j++)
        x_lp4[j] = x_lp[2 * j];
    for (int j = 0; j < lag >> 2; j++)
        y_lp4[j] = y[2 * j];

    /* Coarse search with 4x decimation */
    pitch_xcorr(&x_lp4[0], &y_lp4[0], &xcorr[0], len >> 2, max_pitch >> 2);
    find_best_pitch(&xcorr[0], &y_lp4[0], len >> 2, max_pitch >> 2, best_pitch);

    /* Finer search with 2x decimation */
    for (int i = 0; i < max_pitch >> 1; i++)
    {
        xcorr[i] = 0;
        if (std::abs(i - 2 * best_pitch[0]) > 2 && std::abs(i - 2 * best_pitch[1]) > 2)
            continue;
        _REAL sum = 0;
        for (int j = 0; j < len >> 1; j++)
            sum += x_lp[j] * y[i + j];
        xcorr[i] = std::max(_REAL(-1), sum);
    }
    find_best_pitch(&xcorr[0], y, len >> 1, max_pitch >> 1, best_pitch);

    /* Refine by pseudo-interpolation */
    int offset = 0;
    if (best_pitch[0] > 0 && best_pitch[0] < (max_pitch >> 1) - 1)
    {
        const _REAL a = xcorr[best_pitch[0] - 1];
        const _REAL b = xcorr[best_pitch[0]];
        const _REAL c = xcorr[best_pitch[0] + 1];
        if ((c - a) > .7 * (b - a))
            offset = 1;
        else if ((a - c) > .7 * (b - c))
            offset = -1;
    }
    *pitch = 2 * best_pitch[0] - offset;
}

/* ---- CPcmConcealer ------------------------------------------------------ */

CPcmConcealer::CPcmConcealer() : iFs(0), iHistLen(0), iMaxPeriod(0), iLagMax(0),
    iLagMin(0), iOverlap(0), iNoiseStart(0), iFilled(0), iPitch(0),
    lLostSamples(0), bHaveTail(false), uSeed(22222)
{
}

void CPcmConcealer::Init(int fs)
{
    iFs = fs;
    /* CELT's sizes at 48 kHz, scaled to the output rate, kept even */
    const _REAL s = _REAL(fs) / 48000.0;
    iHistLen = 2 * int(1024 * s);
    iMaxPeriod = 2 * int(512 * s);
    iLagMax = 4 * int(180 * s);
    iLagMin = std::max(4, int(100 * s));
    iOverlap = std::max(8, int(120 * s));
    iNoiseStart = fs * 80 / 1000; /* periodic for 80 ms, then noise */
    window.resize(iOverlap);
    for (int i = 0; i < iOverlap; i++)
    {
        const _REAL t = sin(.5 * M_PI * (i + .5) / iOverlap);
        window[i] = sin(.5 * M_PI * t * t);
    }
    for (int c = 0; c < 2; c++)
    {
        lpc[c].assign(LPC_ORDER, 0.0);
        tail[c].assign(iOverlap, 0.0);
    }
    Reset();
}

void CPcmConcealer::Reset()
{
    for (int c = 0; c < 2; c++)
        hist[c].assign(iHistLen, 0.0);
    iFilled = 0;
    lLostSamples = 0;
    bHaveTail = false;
}

static void push(std::vector<_REAL>& h, const _REAL* x, int N)
{
    const int H = int(h.size());
    if (N >= H)
    {
        std::copy(x + N - H, x + N, h.begin());
        return;
    }
    std::move(h.begin() + N, h.end(), h.begin());
    std::copy(x, x + N, h.end() - N);
}

void CPcmConcealer::Store(CVector<_REAL>& left, CVector<_REAL>& right, bool bLost)
{
    const int N = left.Size();
    if (iHistLen == 0 || N <= 0)
        return;
    CVector<_REAL>* ch[2] = {&left, &right};
    for (int c = 0; c < 2; c++)
    {
        _REAL* x = &(*ch[c])[0];
        if (bHaveTail)
        {
            /* cross-fade from the extrapolation into the decoded frame */
            for (int i = 0; i < std::min(iOverlap, N); i++)
            {
                const _REAL a = window[i] * window[i];
                x[i] = a * x[i] + (1 - a) * tail[c][i];
            }
        }
        push(hist[c], x, N);
    }
    iFilled = std::min(iHistLen, iFilled + N);
    lLostSamples = bLost ? lLostSamples + N : 0;
    bHaveTail = false;
}

void CPcmConcealer::PitchSearch()
{
    /* celt_plc_pitch_search() */
    const int half = iHistLen >> 1;
    std::vector<_REAL> lp(half);
    const _REAL* x[2] = {&hist[0][0], &hist[1][0]};
    pitch_downsample(x, &lp[0], half, 2, 2);
    int idx = 0;
    pitch_search(&lp[iLagMax >> 1], &lp[0], iHistLen - iLagMax, iLagMax - iLagMin, &idx);
    iPitch = std::min(iLagMax, std::max(iLagMin, iLagMax - idx));
}

void CPcmConcealer::ComputeLpc(int c)
{
    /* LPC of the last MAX_PERIOD samples before the loss */
    _REAL ac[LPC_ORDER + 1];
    celt_autocorr(&hist[c][iHistLen - iMaxPeriod], ac, &window[0], iOverlap, LPC_ORDER, iMaxPeriod);
    /* Add a noise floor of -40 dB. */
    ac[0] *= 1.0001;
    /* Use lag windowing to stabilize the Levinson-Durbin recursion. */
    for (int i = 1; i <= LPC_ORDER; i++)
        ac[i] -= ac[i] * (0.008 * 0.008) * i * i;
    celt_lpc(&lpc[c][0], ac, LPC_ORDER);
}

bool CPcmConcealer::Conceal(CVector<_REAL>& left, CVector<_REAL>& right, int N)
{
    if (iHistLen == 0 || N <= 0 || iFilled < iHistLen)
        return false;

    const bool bFirst = lLostSamples == 0;
    if (bFirst)
    {
        PitchSearch();
        for (int c = 0; c < 2; c++)
            ComputeLpc(c);
    }
    /* celt fades by 0.8 per (20 ms) frame after the first lost frame */
    const _REAL fade = bFirst ? 1.0 : pow(0.8, _REAL(N) / (0.02 * iFs));
    const bool bNoise = lLostSamples >= iNoiseStart;
    const int pitch = iPitch;
    const int excLength = std::min(2 * pitch, iMaxPeriod);
    const int L = N + iOverlap;

    left.Init(N);
    right.Init(N);
    CVector<_REAL>* ch[2] = {&left, &right};
    std::vector<_REAL> _exc(iMaxPeriod + LPC_ORDER), y(L), fir(excLength);

    for (int c = 0; c < 2; c++)
    {
        const std::vector<_REAL>& buf = hist[c];
        _REAL* exc = &_exc[LPC_ORDER];
        for (int i = 0; i < iMaxPeriod + LPC_ORDER; i++)
            exc[i - LPC_ORDER] = buf[iHistLen - iMaxPeriod - LPC_ORDER + i];

        /* Compute the excitation for exc_length samples before the loss */
        celt_fir(exc + iMaxPeriod - excLength, &lpc[c][0], &fir[0], excLength, LPC_ORDER);
        std::copy(fir.begin(), fir.end(), exc + iMaxPeriod - excLength);

        /* Check if the waveform is decaying, and if so how fast */
        _REAL E1 = 1, E2 = 1;
        const int decay_length = excLength >> 1;
        for (int i = 0; i < decay_length; i++)
        {
            _REAL e = exc[iMaxPeriod - decay_length + i];
            E1 += e * e;
            e = exc[iMaxPeriod - 2 * decay_length + i];
            E2 += e * e;
        }
        E1 = std::min(E1, E2);
        const _REAL decay = sqrt(E1 / E2);

        _REAL S1 = 0;
        if (!bNoise)
        {
            /* Extrapolate from the end of the excitation with a period of
               "pitch", scaling down each period by "decay" */
            const int extrapolation_offset = iMaxPeriod - pitch;
            _REAL attenuation = fade * decay;
            for (int i = 0, j = 0; i < L; i++, j++)
            {
                if (j >= pitch)
                {
                    j -= pitch;
                    attenuation *= decay;
                }
                y[i] = attenuation * exc[extrapolation_offset + j];
                const _REAL tmp = buf[iHistLen - pitch + j];
                S1 += tmp * tmp;
            }
        }
        else
        {
            /* noise with the energy of the last excitation period, 1.5 dB
               down (CELT's noise PLC decays 1.5 dB, then 0.5 dB per frame) */
            _REAL Ee = 0, Es = 0;
            for (int i = 0; i < excLength; i++)
                Ee += exc[iMaxPeriod - excLength + i] * exc[iMaxPeriod - excLength + i];
            for (int i = 0; i < L; i++)
            {
                const _REAL tmp = buf[iHistLen - 1 - (i % iMaxPeriod)];
                Es += tmp * tmp;
            }
            const _REAL g = fade * 0.841 * sqrt(Ee / excLength) * sqrt(3.0);
            for (int i = 0; i < L; i++)
            {
                uSeed = 1664525u * uSeed + 1013904223u; /* celt_lcg_rand */
                y[i] = g * (_REAL(int(uSeed >> 16) - 32768) / 32768.0);
            }
            S1 = Es * fade * fade;
        }

        /* Copy the last samples to the synthesis filter memory so we have a
           continuous signal, then convert the excitation back into the
           signal domain */
        _REAL lpc_mem[LPC_ORDER];
        for (int i = 0; i < LPC_ORDER; i++)
            lpc_mem[i] = buf[iHistLen - 1 - i];
        celt_iir(&y[0], &lpc[c][0], &y[0], L, LPC_ORDER, lpc_mem);

        /* Check if the synthesis energy is higher than expected, which can
           happen with the signal changes during our window. If so, attenuate. */
        _REAL S2 = 0;
        for (int i = 0; i < L; i++)
            S2 += y[i] * y[i];
        if (!(S1 > 0.2 * S2))
        {
            std::fill(y.begin(), y.end(), 0.0);
        }
        else if (S1 < S2)
        {
            const _REAL ratio = sqrt((S1 + 1) / (S2 + 1));
            for (int i = 0; i < iOverlap; i++)
                y[i] *= 1.0 - window[i] * (1.0 - ratio);
            for (int i = iOverlap; i < L; i++)
                y[i] *= ratio;
        }

        /* cross-fade from the previous extrapolation */
        if (bHaveTail)
        {
            for (int i = 0; i < std::min(iOverlap, L); i++)
            {
                const _REAL a = window[i] * window[i];
                y[i] = a * y[i] + (1 - a) * tail[c][i];
            }
        }

        CVector<_REAL>& out = *ch[c];
        for (int i = 0; i < N; i++)
            out[i] = y[i];
        std::copy(y.begin() + N, y.begin() + N + iOverlap, tail[c].begin());
        push(hist[c], &y[0], N);
    }
    lLostSamples += N;
    bHaveTail = true;
    return true;
}
