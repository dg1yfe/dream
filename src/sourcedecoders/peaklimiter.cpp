/******************************************************************************\
 * Look-ahead peak limiter for the decoded audio, see peaklimiter.h
 *
 * Gain computation per stereo sample n (L = look-ahead):
 *   r(n) = min(1, T / max(|l(n)|, |r(n)|))       required gain
 *   m(n) = min r(k), k = n-L+1 .. n                sliding minimum
 *   s(n) = mean m(k), k = n-L+1 .. n               smooth attack over L samples
 *   g(n) = min(s(n), g(n-1) + (1 - g(n-1)) a)      exponential release
 *   y(n) = g(n) x(n-L+1)
 * For a peak at p, every m(k) with k = p .. p+L-1 is <= r(p), so the gain
 * applied to x(p) (at n = p+L-1) is <= r(p): |y| <= T.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
\******************************************************************************/

#include "peaklimiter.h"
#include <cmath>
#include <algorithm>

CPeakLimiter::CPeakLimiter() : iLookAhead(1), rThreshold(32600.0), rReleaseCoef(0.0),
    iDelayPos(0), iAvgPos(0), rAvgSum(0.0), iAvgBelowOne(0), rGain(1.0),
    lTime(0), lLimited(0)
{
}

void CPeakLimiter::Init(int fs, _REAL rNewThreshold)
{
    if (fs <= 0)
        fs = 48000;
    rThreshold = rNewThreshold;
    iLookAhead = std::max(1, int(0.0015 * fs + 0.5));   /* 1.5 ms */
    rReleaseCoef = 1.0 - exp(-1.0 / (0.060 * fs));      /* 60 ms */
    Reset();
}

void CPeakLimiter::Reset()
{
    delayL.assign(iLookAhead - 1, 0.0);
    delayR.assign(iLookAhead - 1, 0.0);
    iDelayPos = 0;
    minQueue.clear();
    avgBuf.assign(iLookAhead, 1.0);
    iAvgPos = 0;
    rAvgSum = iLookAhead;
    iAvgBelowOne = 0;
    rGain = 1.0;
    lTime = 0;
}

void CPeakLimiter::Process(CVector<_REAL>& left, CVector<_REAL>& right, int N)
{
    const int D = iLookAhead - 1;
    for (int i = 0; i < N; i++)
    {
        const _REAL xl = left[i], xr = right[i];

        /* required gain for the new sample */
        const _REAL peak = std::max(std::fabs(xl), std::fabs(xr));
        const _REAL req = peak > rThreshold ? rThreshold / peak : 1.0;

        /* sliding minimum over the last L samples (monotonic queue) */
        while (!minQueue.empty() && minQueue.back().second >= req)
            minQueue.pop_back();
        minQueue.push_back(std::make_pair(lTime, req));
        while (minQueue.front().first <= lTime - iLookAhead)
            minQueue.pop_front();
        const _REAL m = minQueue.front().second;

        /* moving average of m over the last L samples; exactly 1 while all
           entries are 1 (no rounding drift in the transparent case) */
        if (avgBuf[iAvgPos] < 1.0)
            iAvgBelowOne--;
        if (m < 1.0)
            iAvgBelowOne++;
        avgBuf[iAvgPos] = m;
        iAvgPos = (iAvgPos + 1) % iLookAhead;
        _REAL s = 1.0;
        if (iAvgBelowOne > 0)
        {
            rAvgSum = 0.0;
            for (int k = 0; k < iLookAhead; k++)
                rAvgSum += avgBuf[k];
            s = rAvgSum / iLookAhead;
        }

        /* release */
        _REAL g = rGain + (1.0 - rGain) * rReleaseCoef;
        if (g > 1.0 - 1e-6)
            g = 1.0;
        g = std::min(g, s);
        rGain = g;

        /* delay line */
        _REAL yl = xl, yr = xr;
        if (D > 0)
        {
            yl = delayL[iDelayPos];
            yr = delayR[iDelayPos];
            delayL[iDelayPos] = xl;
            delayR[iDelayPos] = xr;
            iDelayPos = (iDelayPos + 1) % D;
        }
        if (g < 1.0)
        {
            yl *= g;
            yr *= g;
            lLimited++;
        }
        left[i] = yl;
        right[i] = yr;
        lTime++;
    }
}
