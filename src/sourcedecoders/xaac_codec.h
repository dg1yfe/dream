/******************************************************************************\
 *
 * Description:
 *  DRM AAC / HE-AAC / HE-AACv2 encoder based on libxaac (see xaac_codec.cpp)
 *
 ******************************************************************************
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
\******************************************************************************/

#ifndef XAAC_CODEC_H_
#define XAAC_CODEC_H_

#include "AudioCodec.h"
#include <string>
#include <vector>

/* Encoder only (DRM AAC-LC, HE-AAC, HE-AAC v2 with exact access unit sizes).
   libxaacenc_drm is loaded at run time; without it CanEncode() is false and the
   codec list falls back to the next encoder (faac). DREAM_NO_XAAC=1 disables it. */
class XaacCodec : public CAudioCodec
{
public:
    XaacCodec();
    virtual ~XaacCodec();
    /* Decoder: not provided */
    virtual std::string DecGetVersion();
    virtual bool CanDecode(CAudioParam::EAudCod eAudioCoding);
    virtual bool DecOpen(const CAudioParam& AudioParam, int& iAudioSampleRate);
    virtual EDecError Decode(const std::vector<uint8_t>& audio_frame, uint8_t aac_crc_bits,
                             CVector<_REAL>& left, CVector<_REAL>& right);
    virtual void DecClose();
    virtual void DecUpdate(CAudioParam& AudioParam);
    /* Encoder */
    virtual std::string EncGetVersion();
    virtual bool CanEncode(CAudioParam::EAudCod eAudioCoding);
    virtual bool CanEncodeSBR(CAudioParam::EAudCod c) { return CanEncode(c); }
    virtual bool CanEncodePS(CAudioParam::EAudCod c) { return CanEncode(c); }
    virtual bool EncOpen(const CAudioParam& AudioParam, unsigned long& lNumSampEncIn,
                         unsigned long& lMaxBytesEncOut);
    virtual int Encode(CVector<_SAMPLE>& vecsEncInData, unsigned long lNumSampEncIn,
                       CVector<uint8_t>& vecsEncOutData, unsigned long lMaxBytesEncOut);
    virtual void EncClose();
    virtual void EncSetBitrate(int iBitRate);
    virtual void EncUpdate(CAudioParam& AudioParam);
    virtual bool EncHasExactFrameSize() const { return true; }
    virtual void EncSetNextFrameSize(unsigned long lBytes);
    virtual bool EncHasFrameSizeRange() const { return true; }
    virtual void EncSetNextFrameSizeRange(unsigned long lMinBytes, unsigned long lMaxBytes);
    virtual std::string fileName(const CParameter& Parameters) const;

    /* the library could be loaded and is the DREAM DRM build */
    static bool LibLoaded();

private:
    bool Create(int iBitRate);
    void Destroy();

    void* pEnc;           /* libxaac api object */
    void* pInCfg;         /* ixheaace_input_config */
    void* pOutCfg;        /* ixheaace_output_config */
    std::vector<uint8_t> drcCfg; /* pv_drc_cfg (unused for AAC, must exist) */
    int iAot;
    int iChannels;
    int iCoreRate;
    int iInputRate;
    int iBitRate;
    unsigned long lNextFrameBytes;    /* max */
    unsigned long lNextFrameBytesMin;
};

#endif // XAAC_CODEC_H_
