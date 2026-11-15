/******************************************************************************\
 *
 * Copyright (c) 2013
 *
 * Author(s):
 *  David Flamand
 *
 * Description:
 *  See aac_codec.cpp
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

#ifndef FDK_AAC_CODEC_H_
#define FDK_AAC_CODEC_H_

#include "AudioCodec.h"
#include "fdk_dll.h"   /* fdk-aac headers; the library itself is loaded at runtime */

class FdkAacCodec : public CAudioCodec
{
public:
    FdkAacCodec();
    virtual ~FdkAacCodec();
	/* Decoder */
	virtual std::string DecGetVersion();
	virtual bool CanDecode(CAudioParam::EAudCod eAudioCoding);
    virtual bool DecOpen(const CAudioParam& AudioParam, int& iAudioSampleRate);
    virtual EDecError Decode(const std::vector<uint8_t>& audio_frame, uint8_t aac_crc_bits, CVector<_REAL>& left,  CVector<_REAL>& right);
    virtual void DecClose();
	virtual void DecUpdate(CAudioParam& AudioParam);
    /* Encoder */
	virtual std::string EncGetVersion();
	virtual bool CanEncode(CAudioParam::EAudCod eAudioCoding);
	/* the patched fdk-aac DRM encoder does AAC+SBR(+PS) */
	virtual bool CanEncodeSBR(CAudioParam::EAudCod c) { return CanEncode(c); }
	virtual bool CanEncodePS(CAudioParam::EAudCod c) { return CanEncode(c); }
    virtual bool EncOpen(const CAudioParam& AudioParam, unsigned long& lNumSampEncIn, unsigned long& lMaxBytesEncOut);
    virtual int Encode(CVector<_SAMPLE>&, unsigned long, CVector<uint8_t>&, unsigned long);
    virtual void EncClose();
	virtual void EncSetBitrate(int iBitRate);
	virtual void EncUpdate(CAudioParam& AudioParam);
    /* DRM: exact size (bytes, aac_crc_bits included) of the next encoded
       frame, so that an audio super frame is filled completely (needed for
       SBR, whose data is read backwards from the end of each frame) */
    virtual void EncSetNextFrameSize(unsigned long lBytes);
    virtual bool EncHasExactFrameSize() const { return true; }
    virtual void resetFile(std::string) {}
    virtual std::string fileName(const CParameter& Parameters) const;
protected:
    HANDLE_AACDECODER hDecoder;
    HANDLE_AACENCODER hEncoder;
    bool bUsac;
    int16_t decode_buf[13840];
    /* Encoder state */
    bool bEncDrm;             /* true: DRM AAC (TT_DRM, 960), false: ADTS (AC_MPEGAAC) */
    int iEncCoreSampleRate;   /* AAC core sample rate (12/24/48 kHz for DRM) */
    bool bEncSbrStereo;       /* AAC+SBR with a stereo (CPE) core, not parametric stereo */
    std::vector<INT_PCM> encInBuf;
    std::vector<UCHAR> encOutBuf;
public:
    /* libfdk-aac (loaded at runtime) has the DRM encoder patch */
    static bool HasDrmEncoder();
};

#endif // FDK_AAC_CODEC_H_
