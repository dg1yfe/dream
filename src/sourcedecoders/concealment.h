/******************************************************************************\
 * Codec independent packet loss concealment on the decoded PCM signal
 *
 * Pitch search + LPC extrapolation, then LPC shaped noise, ported from the
 * CELT decoder of Opus (celt/pitch.c, celt/celt_lpc.c, celt_decode_lost() in
 * celt/celt_decoder.c, float path), see concealment.cpp for the BSD licence
 * of that code.
 *
 * Used by CAudioSourceDecoder for short losses (a few audio frames) of
 * decoders that do not conceal themselves. Longer losses and loss of the
 * signal still fade out to silence (Reverb).
\******************************************************************************/

#ifndef CONCEALMENT_H
#define CONCEALMENT_H

#include "../GlobalDefinitions.h"
#include "../util/Vector.h"
#include <vector>

class CPcmConcealer
{
public:
    CPcmConcealer();

    /* fs: output sample rate */
    void Init(int fs);
    /* forget the history (after silence, loss of signal, new service) */
    void Reset();

    /* a frame that is played (decoded or concealed by the codec): store it
       in the history; if the previous frame was concealed here, cross-fade
       the extrapolation into the start of the frame first */
    void Good(CVector<_REAL>& left, CVector<_REAL>& right) { Store(left, right, false); }
    /* a lost frame concealed by the decoder itself: store it, the loss goes on */
    void Concealed(CVector<_REAL>& left, CVector<_REAL>& right) { Store(left, right, true); }

    /* conceal a lost frame of N samples per channel. Returns false (output
       untouched) if there is no usable history */
    bool Conceal(CVector<_REAL>& left, CVector<_REAL>& right, int N);

    /* duration of the current loss in ms (0 if the last frame was good) */
    int LossMs() const { return iFs > 0 ? int(lLostSamples * 1000 / iFs) : 0; }

protected:
    void Store(CVector<_REAL>& left, CVector<_REAL>& right, bool bLost);
    void PitchSearch();
    void ComputeLpc(int c);

    int iFs;
    int iHistLen;       /* DECODE_BUFFER_SIZE (2048 at 48 kHz) */
    int iMaxPeriod;     /* MAX_PERIOD (1024 at 48 kHz) */
    int iLagMax, iLagMin; /* PLC_PITCH_LAG_MAX/MIN (720/100 at 48 kHz) */
    int iOverlap;       /* 120 at 48 kHz (2.5 ms) */
    int iNoiseStart;    /* samples of loss after which noise is used */
    int iFilled;        /* valid samples in the history */
    int iPitch;
    long lLostSamples;
    bool bHaveTail;
    unsigned uSeed;
    std::vector<_REAL> window;      /* CELT MDCT window, iOverlap long */
    std::vector<_REAL> hist[2];     /* per channel, iHistLen */
    std::vector<_REAL> lpc[2];      /* per channel, LPC_ORDER */
    std::vector<_REAL> tail[2];     /* extrapolation beyond the frame */
};

#endif
