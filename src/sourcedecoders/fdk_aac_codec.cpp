/******************************************************************************\
 *
 * Copyright (c) 2013
 *
 * Author(s):
 *  David Flamand
 *
 * Description:
 *  AAC codec class
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

#include "fdk_aac_codec.h"
#include <fdk-aac/aacenc_lib.h>
#include <fdk-aac/FDK_audio.h>
#include "src/SDC/SDC.h"
#include <cstring>
#include <iomanip>

FdkAacCodec::FdkAacCodec() :
    hDecoder(nullptr), hEncoder(nullptr),bUsac(false),decode_buf(),
    bEncDrm(false), iEncCoreSampleRate(0), encInBuf(), encOutBuf()
{
}


FdkAacCodec::~FdkAacCodec()
{
	DecClose();
	EncClose();
}

static void aacinfo(LIB_INFO& inf) {
    LIB_INFO info[FDK_MODULE_LAST];
    memset(info, 0, sizeof(info));
    memset(&inf, 0, sizeof(inf));
    FDKinitLibInfo(info);
    aacDecoder_GetLibInfo(info);
    for(int i=0; i<FDK_MODULE_LAST && info[i].module_id!=FDK_NONE; i++) {
        if(info[i].module_id == FDK_AACDEC) {
            inf = info[i];
        }
    }
}

string
FdkAacCodec::DecGetVersion()
{
    stringstream version;
    LIB_INFO info;
    aacinfo(info);
    version << info.title << " version " << info.versionStr << " " << info.build_date << endl;
    return version.str();
}

bool
FdkAacCodec::CanDecode(CAudioParam::EAudCod eAudioCoding)
{
    LIB_INFO linfo;
    aacinfo(linfo);
    if(eAudioCoding == CAudioParam::AC_AAC) {
        if((linfo.flags & CAPF_AAC_DRM_BSFORMAT) == 0) {
            cerr << "aac but codec has no drm" << endl;
            return false;
        }
        /* SBR/PS capabilities live in the FDK_SBRDEC module entry (the old
           code tested them against the AAC decoder flags, which only passed
           because of overlapping bit values). Open-source fdk-aac 2.0.x
           decodes DRM parametric stereo with the MPEG-4 PS tool and reports
           CAPF_SBR_PS_MPEG only; CAPF_SBR_PS_DRM (legacy DRM PS) is not
           available there. */
        LIB_INFO all[FDK_MODULE_LAST];
        memset(all, 0, sizeof(all));
        FDKinitLibInfo(all);
        aacDecoder_GetLibInfo(all);
        UINT sbrFlags = FDKlibInfo_getCapabilities(all, FDK_SBRDEC);
        if((sbrFlags & CAPF_SBR_DRM_BS) == 0) {
            cerr << "aac but  codec has no sbr" << endl;
            return false;
        }
        if((sbrFlags & (CAPF_SBR_PS_DRM | CAPF_SBR_PS_MPEG)) == 0) {
            cerr << "aac but  codec has no parametric stereo" << endl;
            return false;
        }
        return true;
    }
#ifdef HAVE_USAC
    if(eAudioCoding == CAudioParam::AC_xHE_AAC) {
        if((linfo.flags & CAPF_AAC_USAC) != 0)
            return true;
    }
#endif
    return false;
}

static void logAOT(const CStreamInfo& info) {
    switch (info.aot) {
    case AUDIO_OBJECT_TYPE::AOT_DRM_AAC:
        cerr << " AAC";
        break;
    case AUDIO_OBJECT_TYPE::AOT_DRM_SBR:
        cerr << " AAC+SBR";
        break;
    case AUDIO_OBJECT_TYPE::AOT_DRM_MPEG_PS:
        cerr << " AAC+SBR+PS";
        break;
    case AUDIO_OBJECT_TYPE::AOT_DRM_SURROUND:
        cerr << " AAC+Surround";
        break;
#ifdef HAVE_USAC
    case AUDIO_OBJECT_TYPE::AOT_USAC:
    case AUDIO_OBJECT_TYPE::AOT_DRM_USAC:
        cerr << " xHE-AAC";
        break;
#endif
    default:
        cerr << "unknown object type";
    }
    if(info.extAot == AUDIO_OBJECT_TYPE::AOT_SBR) {
        cerr << "+SBR";
    }
}
    /*
     * AC_ER_VCB11 1 means use  virtual codebooks
     * AC_ER_RVLC  1 means use huffman codeword reordering
     * AC_ER_HCR   1 means use virtual codebooks
     * AC_SCALABLE AAC Scalable
     * AC_ELD AAC-ELD
     * AC_LD AAC-LD
     * AC_ER ER syntax
     * AC_BSAC BSAC
     * AC_USAC USAC
     * AC_RSV603DA RSVD60 3D audio
     * AC_HDAAC HD-AAC
     * AC_RSVD50 Rsvd50
     * AC_SBR_PRESENT SBR present flag (from ASC)
     * AC_SBRCRC  SBR CRC present flag. Only relevant for AAC-ELD for now.
     * AC_PS_PRESENT PS present flag (from ASC or implicit)
     * AC_MPS_PRESENT   MPS present flag (from ASC or implicit)
     * AC_DRM DRM bit stream syntax
     * AC_INDEP Independency flag
     * AC_MPEGD_RES MPEG-D residual individual channel data.
     * AC_SAOC_PRESENT SAOC Present Flag
     * AC_DAB DAB bit stream syntax
     * AC_ELD_DOWNSCALE ELD Downscaled playout
     * AC_LD_MPS Low Delay MPS.
     * AC_DRC_PRESENT  Dynamic Range Control (DRC) data found.
     * AC_USAC_SCFGI3  USAC flag: If stereoConfigIndex is 3 the flag is set.
     */

static void logFlags(const CStreamInfo& info) {

    if((info.flags & AC_USAC) == AC_USAC) {
        cerr << "+USAC";
    }
    if((info.flags & AC_SBR_PRESENT) == AC_SBR_PRESENT) {
        cerr << "+SBR";
    }
    if((info.flags & AC_SBRCRC) == AC_SBRCRC) {
        cerr << "+SBR-CRC";
    }
    if((info.flags & AC_PS_PRESENT) == AC_PS_PRESENT) {
        cerr << "+PS";
    }
    if((info.flags & AC_MPS_PRESENT) == AC_MPS_PRESENT) {
        cerr << "+MPS";
    }
    if((info.flags & AC_INDEP) == AC_INDEP) {
        cerr << " (independent)";
    }
}

static void logNumbers(const CStreamInfo& info) {
    cerr << " channels coded " << info.aacNumChannels
         << " coded sample rate " << info.aacSampleRate
         << " channels " << info.numChannels
         << " channel config " << info.channelConfig
         << " sample rate " << info.sampleRate
         << " extended sample rate " << info.extSamplingRate
         << " samples per frame " << info.aacSamplesPerFrame
         << " decoded audio frame size " << info.frameSize
         << " flags " << hex << info.flags << dec;
    cerr << " channel 0 type " << int(info.pChannelType[0]) << " index " << int(info.pChannelIndices[0]);
    if(info.numChannels==2)
        cerr << " channel 1 type " << int(info.pChannelType[1]) << " index " << int(info.pChannelIndices[1]);
}

/*      FROM AACCodec:
    In case of SBR, AAC sample rate is half the total sample rate. Length of output is doubled if SBR is used
    if (AudioParam.eSBRFlag == CAudioParam::SB_USED)
    {
        iAudioSampleRate = iAACSampleRate * 2;
    }
    else
    {
        iAudioSampleRate = iAACSampleRate;
    }
*/

bool
FdkAacCodec::DecOpen(const CAudioParam& AudioParam, int& iAudioSampleRate)
{
    unsigned type9Size;
    UCHAR *t9;
    hDecoder = aacDecoder_Open (TRANSPORT_TYPE::TT_DRM, 3);

    // provide a default value for iAudioSampleRate in case we can't do better. TODO xHEAAC
    int iDefaultSampleRate;
    switch (AudioParam.eAudioSamplRate)
    {
    case CAudioParam::AS_12KHZ:
        iDefaultSampleRate = 12000;
        break;

    case CAudioParam::AS_24KHZ:
        iDefaultSampleRate = 24000;
        break;
    default:
        iDefaultSampleRate = 12000;
        break;
    }
    if (AudioParam.eSBRFlag == CAudioParam::SB_USED)
    {
        iDefaultSampleRate = iDefaultSampleRate * 2;
    }

    if(hDecoder == nullptr) {
        iAudioSampleRate = iDefaultSampleRate;
        return false;
    }

    vector<uint8_t> type9 = AudioParam.getType9Bytes();
    type9Size = unsigned(type9.size());
    t9 = &type9[0];

    AAC_DECODER_ERROR err = aacDecoder_ConfigRaw (hDecoder, &t9, &type9Size);
    if(err == AAC_DEC_OK) {
        CStreamInfo *pinfo = aacDecoder_GetStreamInfo(hDecoder);
        if (pinfo==nullptr) {
            cerr << "DecOpen No stream info" << endl;
            iAudioSampleRate = iDefaultSampleRate;
            return true;// TODO
        }
        cerr << "DecOpen";
        logAOT(*pinfo);
        logFlags(*pinfo);
        //logNumbers(*pinfo);
        cerr << endl;
        iAudioSampleRate = pinfo->extSamplingRate;

        if(iAudioSampleRate == 0) {
            iAudioSampleRate = iDefaultSampleRate; // get from AudioParam if codec couldn't get it
        }

        if(pinfo->aot == AUDIO_OBJECT_TYPE::AOT_USAC) bUsac = true;
#ifdef HAVE_USAC
        else if(pinfo->aot == AUDIO_OBJECT_TYPE::AOT_DRM_USAC) bUsac = true;
#endif
        else bUsac = false;

        return true;
    }
    iAudioSampleRate = iDefaultSampleRate;  // get from AudioParam if codec couldn't get it
    return true; // TODO
 }

CAudioCodec::EDecError FdkAacCodec::Decode(const vector<uint8_t>& audio_frame, uint8_t aac_crc_bits, CVector<_REAL>& left, CVector<_REAL>& right)
{
    writeFile(audio_frame);
    vector<uint8_t> data;
    uint8_t* pData;
    UINT bufferSize;
    if(bUsac) {
        pData = const_cast<uint8_t*>(&audio_frame[0]);
        bufferSize = UINT(audio_frame.size());
    }
    else {
        data.resize(audio_frame.size()+1);
        data[0] = aac_crc_bits;

        for (size_t i = 0; i < audio_frame.size(); i++)
            data[i + 1] = audio_frame[i];

        pData = &data[0];
        bufferSize = UINT(data.size());
    }
    UINT bytesValid = bufferSize;
    AAC_DECODER_ERROR err = aacDecoder_Fill(hDecoder, &pData, &bufferSize, &bytesValid);
    if(err != AAC_DEC_OK) {
        cerr << "fill failed " << int(err) << endl;
        return CAudioCodec::DECODER_ERROR_UNKNOWN;
    }

    CStreamInfo *pinfo = aacDecoder_GetStreamInfo(hDecoder);
    if (pinfo==nullptr) {
        cerr << "No stream info" << endl;
        return CAudioCodec::DECODER_ERROR_UNKNOWN;
    }

    //cerr << "Decode";
    //logAOT(*pinfo);
    //logFlags(*pinfo);
    //logNumbers(*pinfo);
    //cerr << endl;

    if(pinfo->aacNumChannels == 0) {
        cerr << "zero output channels: " << err << endl;
        //return CAudioCodec::DECODER_ERROR_UNKNOWN;
    }
    else {
        //cerr << pinfo->aacNumChannels << " aac channels " << endl;
    }

    if(err != AAC_DEC_OK) {
        cerr << "Fill failed: " << err << endl;
        return CAudioCodec::DECODER_ERROR_UNKNOWN;
    }
    if (bytesValid != 0) {
        cerr << "Unable to feed all " << bufferSize << " input bytes, bytes left " << bytesValid << endl;
        return CAudioCodec::DECODER_ERROR_UNKNOWN;
    }

    if(pinfo->numChannels == 0) {
        cerr << "zero output channels: " << err << endl;
        //return CAudioCodec::DECODER_ERROR_UNKNOWN;
    }

    /* frameSize/numChannels are only valid after decoding (they still
       describe the previous frame here, or are 0 for the first frame), so
       offer the whole buffer to the decoder */
    memset(decode_buf, 0, sizeof(decode_buf));
    err = aacDecoder_DecodeFrame(hDecoder, decode_buf, int(sizeof(decode_buf)/sizeof(decode_buf[0])), 0);
    pinfo = aacDecoder_GetStreamInfo(hDecoder);
    if (pinfo == nullptr) {
        return CAudioCodec::DECODER_ERROR_UNKNOWN;
    }
    const size_t output_size = size_t(pinfo->frameSize) * size_t(pinfo->numChannels);

    if(err == AAC_DEC_OK) {
        double d = 0.0;
        for(size_t i=0; i<output_size; i++) d += double(decode_buf[i]);
        //cerr << "energy in good frame " << (d/output_size) << endl;
    }
    else if(err == AAC_DEC_PARSE_ERROR) {
        cerr << "error parsing bitstream." << endl;
        return CAudioCodec::DECODER_ERROR_UNKNOWN;
    }
#ifdef HAVE_USAC
    else if(err == AAC_DEC_OUTPUT_BUFFER_TOO_SMALL) {
        cerr << "The provided output buffer is too small." << endl;
        return CAudioCodec::DECODER_ERROR_UNKNOWN;
    }
#endif
    else if(err == AAC_DEC_OUT_OF_MEMORY) {
        cerr << "Heap returned NULL pointer. Output buffer is invalid." << endl;
        return CAudioCodec::DECODER_ERROR_UNKNOWN;
    }
    else if(err == AAC_DEC_UNKNOWN) {
        cerr << "Error condition is of unknown reason, or from a another module. Output buffer is invalid." << endl;
        return CAudioCodec::DECODER_ERROR_UNKNOWN;
    }
    else {
        cerr << "other error " << hex << int(err) << dec << endl;
        return CAudioCodec::DECODER_ERROR_UNKNOWN;
    }

    left.Init(pinfo->frameSize, 0.0);
    right.Init(pinfo->frameSize, 0.0);

    if (pinfo->numChannels == 1)
    {
        /* Mono */
        // << "mono " << pinfo->frameSize << endl;
        for(int i = 0; i<pinfo->frameSize; i++) {
            left[int(i)] = _REAL(decode_buf[i]) / 2.0;
            right[int(i)] = _REAL(decode_buf[i]) / 2.0;
        }
    }
    else
    {
        /* Stereo docs claim non-interleaved but we are getting interleaved! */
        //cerr << "stereo " << iResOutBlockSize << endl;
        for(int i = 0; i<pinfo->frameSize; i++) {
            left[int(i)] = _REAL(decode_buf[2*i]);
            right[int(i)] = _REAL(decode_buf[2*i+1]);
        }
    }
    return CAudioCodec::DECODER_ERROR_OK;
}

void
FdkAacCodec::DecClose()
{
    if (hDecoder != nullptr)
	{
        aacDecoder_Close(hDecoder);
        hDecoder = nullptr;
	}
}

void
FdkAacCodec::DecUpdate(CAudioParam&)
{
}


/* ---------------------------------------------------------------------------
 * Encoder
 *
 * AC_AAC (DRM AAC): needs an fdk-aac built with the DRM encoder patch
 * (macx/patches/fdk-aac-2.0.3-drm-encoder.patch): TT_DRM transport, 960
 * sample granule, VCB11 + HCR, aac_crc_bits, SBR bit-reversed at the end of
 * the AU. Each encoded AU is [aac_crc_bits (1 byte)][audio_frame], which is
 * exactly what CAudioSourceEncoderImplementation::ProcessDataInternal() expects.
 * Stock fdk-aac has no DRM encoder: CanEncode(AC_AAC) is false then and the
 * codec list falls back to AacCodec (libfaac_drm).
 *
 * AC_MPEGAAC: plain ADTS AAC (RSCI/AM rebroadcast path).
 * ------------------------------------------------------------------------- */

static UINT fdkEncCaps(FDK_MODULE_ID module)
{
    LIB_INFO info[FDK_MODULE_LAST];
    memset(info, 0, sizeof(info));
    FDKinitLibInfo(info);
    if (aacEncGetLibInfo(info) != AACENC_OK)
        return 0;
    return FDKlibInfo_getCapabilities(info, module);
}

bool
FdkAacCodec::HasDrmEncoder()
{
    const UINT aac = fdkEncCaps(FDK_AACENC);
    const UINT tp = fdkEncCaps(FDK_TPENC);
    const UINT sbr = fdkEncCaps(FDK_SBRENC);
    return (aac & CAPF_AAC_DRM_BSFORMAT) && (aac & CAPF_AAC_960)
        && (tp & CAPF_DRM) && (sbr & CAPF_SBR_DRM_BS);
}

string
FdkAacCodec::EncGetVersion()
{
    LIB_INFO info[FDK_MODULE_LAST];
    memset(info, 0, sizeof(info));
    FDKinitLibInfo(info);
    aacEncGetLibInfo(info);
    for (int i = 0; i < FDK_MODULE_LAST && info[i].module_id != FDK_NONE; i++) {
        if (info[i].module_id == FDK_AACENC) {
            stringstream version;
            version << info[i].title << " version " << info[i].versionStr
                    << (HasDrmEncoder() ? " (DRM)" : "");
            return version.str();
        }
    }
    return "fdk-aac (unknown version)";
}

bool
FdkAacCodec::CanEncode(CAudioParam::EAudCod eAudioCoding)
{
    switch (eAudioCoding) {
    case CAudioParam::AC_MPEGAAC:
        return true;
    case CAudioParam::AC_AAC:
        /* DREAM_NO_FDK_ENCODER=1 hides the fdk DRM encoder (e.g. to test the
           faac fallback); decoding is not affected */
        if (getenv("DREAM_NO_FDK_ENCODER") != nullptr && atoi(getenv("DREAM_NO_FDK_ENCODER")) != 0)
            return false;
        return HasDrmEncoder();
    default:
        return false;
    }
}

bool
FdkAacCodec::EncOpen(const CAudioParam& AudioParam, unsigned long& lNumSampEncIn, unsigned long& lMaxBytesEncOut)
{
    lNumSampEncIn = 0;
    lMaxBytesEncOut = 0;
    EncClose();

    bEncDrm = (AudioParam.eAudioCoding == CAudioParam::AC_AAC);
    if (bEncDrm && !HasDrmEncoder()) {
        cerr << "fdk-aac: library has no DRM encoder" << endl;
        return false;
    }

    switch (AudioParam.eAudioSamplRate) {
    case CAudioParam::AS_12KHZ: iEncCoreSampleRate = 12000; break;
    case CAudioParam::AS_24KHZ: iEncCoreSampleRate = 24000; break;
    case CAudioParam::AS_48KHZ: iEncCoreSampleRate = 48000; break; /* DRM+ */
    default:
        cerr << "fdk-aac: unsupported AAC sample rate" << endl;
        return false;
    }

    const bool bSbr = (AudioParam.eSBRFlag == CAudioParam::SB_USED)
                      || (AudioParam.eAudioMode == CAudioParam::AM_P_STEREO);
    UINT aot = AOT_AAC_LC;
    CHANNEL_MODE chMode = MODE_1;
    UINT nCh = 1;
    switch (AudioParam.eAudioMode) {
    case CAudioParam::AM_MONO:
        aot = bSbr ? AOT_SBR : AOT_AAC_LC;
        break;
    case CAudioParam::AM_P_STEREO:  /* stereo input, mono core + SBR + PS */
        aot = AOT_PS; chMode = MODE_2; nCh = 2;
        break;
    case CAudioParam::AM_STEREO:
        aot = bSbr ? AOT_SBR : AOT_AAC_LC; chMode = MODE_2; nCh = 2;
        break;
    default:
        cerr << "fdk-aac: unsupported audio mode" << endl;
        return false;
    }
    /* with SBR the encoder input runs at twice the AAC core rate */
    const UINT inputRate = UINT(iEncCoreSampleRate * (bSbr ? 2 : 1));

    AACENC_ERROR r = aacEncOpen(&hEncoder, 0, nCh); /* all modules */
    if (r != AACENC_OK) {
        cerr << "fdk-aac: error opening encoder " << hex << r << dec << endl;
        hEncoder = nullptr;
        return false;
    }

    struct { AACENC_PARAM p; UINT v; const char* n; } params[] = {
        { AACENC_AOT,            aot,                                   "AOT" },
        { AACENC_TRANSMUX,       UINT(bEncDrm ? TT_DRM : TT_MP4_ADTS),  "transport" },
        { AACENC_GRANULE_LENGTH, UINT(bEncDrm ? 960 : 1024),            "granule length" },
        { AACENC_SAMPLERATE,     inputRate,                             "sample rate" },
        { AACENC_CHANNELMODE,    UINT(chMode),                          "channel mode" },
        { AACENC_CHANNELORDER,   1,                                     "channel order" },
        /* provisional bitrate, the real one comes from EncSetBitrate() */
        { AACENC_BITRATE,        UINT(bEncDrm ? 16000 * nCh : 64000 * nCh), "bitrate" },
    };
    for (const auto& prm : params) {
        r = aacEncoder_SetParam(hEncoder, prm.p, prm.v);
        if (r != AACENC_OK) {
            cerr << "fdk-aac: error setting " << prm.n << " = " << prm.v << ": " << hex << r << dec << endl;
            EncClose();
            return false;
        }
    }

    if (bEncDrm && bSbr) {
        /* DRM receivers may tune in at any time: SBR header in every AU */
        r = aacEncoder_SetParam(hEncoder, AACENC_HEADER_PERIOD, 1);
        if (r != AACENC_OK) {
            cerr << "fdk-aac: error setting header period: " << hex << r << dec << endl;
            EncClose();
            return false;
        }
    }

    r = aacEncEncode(hEncoder, nullptr, nullptr, nullptr, nullptr);
    if (r != AACENC_OK) {
        cerr << "fdk-aac: error initialising encoder " << hex << r << dec << endl;
        EncClose();
        return false;
    }
    AACENC_InfoStruct info;
    r = aacEncInfo(hEncoder, &info);
    if (r != AACENC_OK) {
        cerr << "fdk-aac: error getting encoder info " << hex << r << dec << endl;
        EncClose();
        return false;
    }

    /* samples per call, all channels (DREAM divides by the channel count) */
    lNumSampEncIn = info.frameLength * info.inputChannels;
    lMaxBytesEncOut = info.maxOutBufBytes;
    encInBuf.assign(lNumSampEncIn, 0);
    encOutBuf.assign(lMaxBytesEncOut, 0);
    return true;
}

int
FdkAacCodec::Encode(CVector<_SAMPLE>& vecsEncInData, unsigned long lNumSampEncIn, CVector<uint8_t>& vecsEncOutData, unsigned long lMaxBytesEncOut)
{
    if (hEncoder == nullptr || encInBuf.empty())
        return 0;
    if (lNumSampEncIn > encInBuf.size()) {
        cerr << "fdk-aac: too many input samples " << lNumSampEncIn << endl;
        return 0;
    }

    for (unsigned long i = 0; i < lNumSampEncIn; i++)
        encInBuf[i] = INT_PCM(vecsEncInData[int(i)]);

    void* inPtr = encInBuf.data();
    INT inId = IN_AUDIO_DATA;
    INT inSize = INT(lNumSampEncIn * sizeof(INT_PCM));
    INT inElSize = sizeof(INT_PCM);
    void* outPtr = encOutBuf.data();
    INT outId = OUT_BITSTREAM_DATA;
    INT outSize = INT(encOutBuf.size());
    INT outElSize = sizeof(UCHAR);

    AACENC_BufDesc inBufDesc = { 1, &inPtr, &inId, &inSize, &inElSize };
    AACENC_BufDesc outBufDesc = { 1, &outPtr, &outId, &outSize, &outElSize };
    AACENC_InArgs inargs = {};
    AACENC_OutArgs outargs = {};
    inargs.numInSamples = INT(lNumSampEncIn);

    AACENC_ERROR err = aacEncEncode(hEncoder, &inBufDesc, &outBufDesc, &inargs, &outargs);
    if (err != AACENC_OK) {
        cerr << "fdk-aac: encoder error " << hex << err << dec << endl;
        return 0;
    }
    const int bytesEncoded = outargs.numOutBytes;
    if (bytesEncoded <= 0)
        return 0; /* encoder still filling its look-ahead */
    if (unsigned(bytesEncoded) > lMaxBytesEncOut || bytesEncoded > vecsEncOutData.Size()) {
        cerr << "fdk-aac: output buffer too small" << endl;
        return 0;
    }
    for (int i = 0; i < bytesEncoded; i++)
        vecsEncOutData[i] = encOutBuf[size_t(i)];
    return bytesEncoded;
}

void
FdkAacCodec::EncClose()
{
    if (hEncoder != nullptr)
	{
        aacEncClose(&hEncoder);
        hEncoder = nullptr;
	}
}

void
FdkAacCodec::EncSetBitrate(int iBitRate)
{
    if (hEncoder == nullptr || iBitRate <= 0)
        return;
    UINT rate = UINT(iBitRate);
    if (bEncDrm) {
        /* DREAM's bitrate is the audio payload per frame without the
           aac_crc_bits byte. fdk counts the CRC byte as part of each AU, and
           its CBR mode lets a single AU exceed the average by one byte (the
           minimum bit reservoir), which over a run of frames could overflow
           the audio super frame. Using the payload rate itself as the AU
           rate (CRC byte included) makes the largest possible AU
           (average + 1 byte) exactly payload + CRC, so every AU fits its
           slot. Round to whole bytes per 960-sample frame. */
        const long bitsPerFrame = (long(iBitRate) * 960 / iEncCoreSampleRate) & ~7L;
        rate = UINT(bitsPerFrame * iEncCoreSampleRate / 960);
    }
    AACENC_ERROR r = aacEncoder_SetParam(hEncoder, AACENC_BITRATE, rate);
    if (r == AACENC_OK && bEncDrm) {
        /* no AU above the average: frame sizes must fit the super frame */
        r = aacEncoder_SetParam(hEncoder, AACENC_PEAK_BITRATE, rate);
    }
    if (r == AACENC_OK) {
        r = aacEncEncode(hEncoder, nullptr, nullptr, nullptr, nullptr); /* re-init */
    }
    if (r != AACENC_OK) {
        cerr << "fdk-aac: cannot set bitrate " << rate << " bit/s: " << hex << r << dec << endl;
    }
}

void
FdkAacCodec::EncUpdate(CAudioParam&)
{
}

void
FdkAacCodec::EncSetNextFrameSize(unsigned long lBytes)
{
    if (hEncoder == nullptr || !bEncDrm)
        return;
    /* AACENC_DRM_AU_BYTES is provided by the DRM encoder patch; ignore
       failures (library without it keeps its peak bitrate control) */
    if (lBytes > encOutBuf.size())
        lBytes = encOutBuf.size();
    /* numeric value: builds against stock fdk-aac headers as well */
    const AACENC_PARAM AACENC_DRM_AU_BYTES_PARAM = AACENC_PARAM(0x0310);
    aacEncoder_SetParam(hEncoder, AACENC_DRM_AU_BYTES_PARAM, UINT(lBytes));
}

string
FdkAacCodec::fileName(const CParameter& Parameters) const
{
    // Store AAC-data in file
    stringstream ss;
    ss << "aac_";

    const CAudioParam& audioParam = Parameters.Service[unsigned(Parameters.GetCurSelAudioService())].AudioParam;

//    Parameters.Lock(); // TODO CAudioSourceDecoder::InitInternal() already have the lock
    if (audioParam.eAudioSamplRate == CAudioParam::AS_12KHZ)
    {
        ss << "12kHz_";
    }
    else
        ss << "24kHz_";

    switch (audioParam.eAudioMode)
    {
    case CAudioParam::AM_MONO:
        ss << "mono";
        break;

    case CAudioParam::AM_P_STEREO:
        ss << "pstereo";
        break;

    case CAudioParam::AM_STEREO:
        ss << "stereo";
        break;
    case CAudioParam::AM_RESERVED:;
    }

    if (audioParam.eSBRFlag == CAudioParam::SB_USED)
    {
        ss << "_sbr";
    }
    ss << ".dat";

    return ss.str();
}
