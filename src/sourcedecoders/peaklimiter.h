/******************************************************************************\
 * Look-ahead peak limiter for the decoded audio (before 16 bit quantisation)
 *
 * Decoders, SBR in particular, may produce peaks above full scale. Instead of
 * hard clipping them in Real2Sample(), the gain is lowered smoothly ahead of
 * such a peak (look-ahead = latency, ~1.5 ms) and released slowly (~60 ms).
 * As long as no sample exceeds the threshold the gain is exactly 1, so the
 * output is the input delayed by the look-ahead, bit-exact; zeros stay zeros.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
\******************************************************************************/

#ifndef PEAKLIMITER_H
#define PEAKLIMITER_H

#include "../GlobalDefinitions.h"
#include "../util/Vector.h"
#include <vector>
#include <deque>

class CPeakLimiter
{
public:
    CPeakLimiter();

    /* fs: sample rate; rThreshold: peak level the output does not exceed */
    void Init(int fs, _REAL rThreshold = 32600.0); /* -0.04 dBFS */
    void Reset();

    /* in place, N samples per channel; the output is delayed by
       GetLatency() samples */
    void Process(CVector<_REAL>& left, CVector<_REAL>& right, int N);

    int GetLatency() const { return iLookAhead - 1; }
    long GetLimitedSamples() const { return lLimited; }

protected:
    int iLookAhead;           /* L */
    _REAL rThreshold;
    _REAL rReleaseCoef;
    /* delay line (L-1 samples per channel) */
    std::vector<_REAL> delayL, delayR;
    int iDelayPos;
    /* sliding minimum of the required gain over the last L samples */
    std::deque<std::pair<long, _REAL> > minQueue;
    /* moving average of the sliding minimum over the last L samples */
    std::vector<_REAL> avgBuf;
    int iAvgPos;
    _REAL rAvgSum;
    int iAvgBelowOne;         /* entries < 1 in avgBuf */
    _REAL rGain;              /* current gain (with release) */
    long lTime;
    long lLimited;            /* samples output with gain < 1 */
};

#endif
