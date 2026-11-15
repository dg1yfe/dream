/******************************************************************************\
 *
 * Description:
 *  DRM AAC-LC / HE-AAC / HE-AAC v2 encoder using libxaac (Google/Ittiam,
 *  Apache-2.0) with DREAM's patches (macx/patches/libxaac-*.patch): 960-sample
 *  frames, 12/24 kHz cores, ETSI ES 201 980 access units (ER AAC with VCB11 and
 *  HCR, aac_crc_bits, SBR/PS data bit-reversed at the end of the AU) of exactly
 *  the size the audio super frame asks for. The library is loaded at run time
 *  (libxaacenc_drm, built by macx/build-xaac.sh).
 *
 *  Output of Encode(): aac_crc_bits byte followed by the access unit, as
 *  CAudioSourceEncoderImplementation::ProcessDataInternal() expects.
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

#include "xaac_codec.h"
#include "xaac_dll.h"
#include "../util/LibraryLoader.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <sstream>
#ifndef _WIN32
# include <dlfcn.h>
#endif

/* libxaac public API (installed by macx/build-xaac.sh into include/xaacenc_drm).
   Kept in this translation unit only: ixheaace_api.h #defines AOT_* names that
   clash with fdk-aac's AUDIO_OBJECT_TYPE enum. */
extern "C" {
#include <xaacenc_drm/ixheaac_type_def.h>
#include <xaacenc_drm/ixheaac_error_standards.h>
#include <xaacenc_drm/ixheaace_api.h>
}

using namespace std;

static xaac_get_lib_id_strings_t* xaac_get_lib_id_strings = nullptr;
static xaac_create_t* xaac_create = nullptr;
static xaac_process_t* xaac_process = nullptr;
static xaac_delete_t* xaac_delete = nullptr;

static const LIBFUNC XaacLibFuncs[] = {
    { "ixheaace_get_lib_id_strings", (void**)&xaac_get_lib_id_strings, nullptr },
    { "ixheaace_create", (void**)&xaac_create, nullptr },
    { "ixheaace_process", (void**)&xaac_process, nullptr },
    { "ixheaace_delete", (void**)&xaac_delete, nullptr },
    { nullptr, nullptr, nullptr }
};

#if defined(_WIN32)
static const char* XaacLibNames[] = { "libxaacenc_drm.dll", "xaacenc_drm.dll", nullptr };
#elif defined(__APPLE__)
static const char* XaacLibNames[] = { "libxaacenc_drm.dylib", nullptr };
#else
static const char* XaacLibNames[] = { "libxaacenc_drm.so", nullptr };
#endif

static void* hXaacLib = nullptr;
static string sXaacLibPath;
static string sXaacVersion;

/* only the DREAM build has the DRM access units ("IXHEAACE+DRM") */
static bool XaacLibCheck()
{
    ixheaace_version v;
    memset(&v, 0, sizeof(v));
    if (xaac_get_lib_id_strings == nullptr || xaac_get_lib_id_strings(&v) != 0 || v.p_lib_name == nullptr)
        return false;
    const string name(reinterpret_cast<const char*>(v.p_lib_name));
    if (name.find("+DRM") == string::npos)
        return false;
    sXaacVersion = "libxaac " + name +
        (v.p_version_num ? string(reinterpret_cast<const char*>(v.p_version_num)) : string());
    return true;
}

static bool XaacLibDoLoad()
{
    const char* off = getenv("DREAM_NO_XAAC");
    if (off != nullptr && atoi(off) != 0) {
        cerr << "xaac library not loaded (DREAM_NO_XAAC set): libxaac encoder disabled" << endl;
        return false;
    }
    hXaacLib = CLibraryLoader::Load(XaacLibNames, XaacLibFuncs, XaacLibCheck);
    if (hXaacLib == nullptr) {
        cerr << "xaac library not found (" << XaacLibNames[0]
             << " with DRM support): libxaac encoder disabled" << endl;
        return false;
    }
#ifndef _WIN32
    Dl_info dli;
    if (dladdr(reinterpret_cast<void*>(xaac_create), &dli) != 0 && dli.dli_fname != nullptr)
        sXaacLibPath = dli.dli_fname;
#endif
    if (sXaacLibPath.empty())
        sXaacLibPath = "(unknown path)";
    cerr << "xaac library loaded: " << sXaacLibPath << " (DRM encoder: yes, "
         << sXaacVersion << ")" << endl;
    return true;
}

bool XaacCodec::LibLoaded()
{
    static once_flag once;
    static bool loaded = false;
    call_once(once, [] { loaded = XaacLibDoLoad(); });
    return loaded;
}

static void* xaacMalloc(UWORD32 size, UWORD32 align)
{
    void* p = nullptr;
    if (align < sizeof(void*))
        align = sizeof(void*);
#ifdef _WIN32
    p = _aligned_malloc(size, align);
#else
    if (posix_memalign(&p, align, size) != 0)
        p = nullptr;
#endif
    return p;
}

static void xaacFree(void* p)
{
#ifdef _WIN32
    _aligned_free(p);
#else
    free(p);
#endif
}

XaacCodec::XaacCodec() :
    pEnc(nullptr), pInCfg(nullptr), pOutCfg(nullptr), drcCfg(), iAot(0), iChannels(0),
    iCoreRate(0), iInputRate(0), iBitRate(0), lNextFrameBytes(0), lNextFrameBytesMin(0)
{
}

XaacCodec::~XaacCodec()
{
    EncClose();
}

/* Decoder: not provided ---------------------------------------------------- */
string XaacCodec::DecGetVersion() { return string(); }
bool XaacCodec::CanDecode(CAudioParam::EAudCod) { return false; }
bool XaacCodec::DecOpen(const CAudioParam&, int& iAudioSampleRate) { iAudioSampleRate = 0; return false; }
CAudioCodec::EDecError XaacCodec::Decode(const vector<uint8_t>&, uint8_t, CVector<_REAL>&, CVector<_REAL>&)
{
    return CAudioCodec::DECODER_ERROR_UNKNOWN;
}
void XaacCodec::DecClose() {}
void XaacCodec::DecUpdate(CAudioParam&) {}

/* Encoder ------------------------------------------------------------------ */
string XaacCodec::EncGetVersion()
{
    return LibLoaded() ? sXaacVersion + " (DRM)" : string("libxaac (library not found)");
}

bool XaacCodec::CanEncode(CAudioParam::EAudCod eAudioCoding)
{
    return eAudioCoding == CAudioParam::AC_AAC && LibLoaded();
}

bool XaacCodec::EncOpen(const CAudioParam& AudioParam, unsigned long& lNumSampEncIn, unsigned long& lMaxBytesEncOut)
{
    lNumSampEncIn = 0;
    lMaxBytesEncOut = 0;
    EncClose();
    if (!LibLoaded() || AudioParam.eAudioCoding != CAudioParam::AC_AAC)
        return false;

    switch (AudioParam.eAudioSamplRate) {
    case CAudioParam::AS_12KHZ: iCoreRate = 12000; break;
    case CAudioParam::AS_24KHZ: iCoreRate = 24000; break;
    case CAudioParam::AS_48KHZ: iCoreRate = 48000; break; /* DRM+ (AAC-LC only) */
    default:
        cerr << "xaac: unsupported AAC sample rate" << endl;
        return false;
    }
    const bool bSbr = (AudioParam.eSBRFlag == CAudioParam::SB_USED)
                      || (AudioParam.eAudioMode == CAudioParam::AM_P_STEREO);
    switch (AudioParam.eAudioMode) {
    case CAudioParam::AM_MONO:     iAot = bSbr ? AOT_SBR : AOT_AAC_LC; iChannels = 1; break;
    case CAudioParam::AM_P_STEREO: iAot = AOT_PS; iChannels = 2; break;
    case CAudioParam::AM_STEREO:   iAot = bSbr ? AOT_SBR : AOT_AAC_LC; iChannels = 2; break;
    default:
        cerr << "xaac: unsupported audio mode" << endl;
        return false;
    }
    if (bSbr && iCoreRate > 24000) {
        cerr << "xaac: SBR needs a 12 or 24 kHz AAC core" << endl;
        return false;
    }
    iInputRate = iCoreRate * (bSbr ? 2 : 1);
    if (!Create(16000 * iChannels))
        return false;

    ixheaace_output_config* out = static_cast<ixheaace_output_config*>(pOutCfg);
    cerr << "xaac: DRM encoder opened (AOT " << iAot << ", " << iChannels << " ch, core "
         << iCoreRate << " Hz, input " << iInputRate << " Hz)" << endl;
    /* samples per call, all channels (DREAM divides by the channel count) */
    lNumSampEncIn = static_cast<unsigned long>(out->input_size) / sizeof(int16_t);
    lMaxBytesEncOut = out->mem_info_table[IA_MEMTYPE_OUTPUT].ui_size;
    /* largest access unit libxaac writes in DRM mode (aac_crc_bits included): 5760 bits
       per AAC channel, i.e. 6144 bits scaled to 960 samples (PS has one AAC channel) */
    const unsigned long lMaxAu = 720UL * (iAot == AOT_PS ? 1 : iChannels);
    if (lMaxBytesEncOut > lMaxAu) lMaxBytesEncOut = lMaxAu;
    return true;
}

bool XaacCodec::Create(int iNewBitRate)
{
    Destroy();
    /* The bitrate only selects libxaac's tuning (bandwidth, SBR start band): the size of
       every access unit is set exactly with EncSetNextFrameSize(). Keep it inside the SBR
       tuning tables (stereo/PS rows start at 16 kbit/s, mono below 10 kbit/s has no HF). */
    int iTune = iNewBitRate;
    if (iAot != AOT_AAC_LC) {
        const int iMin = (iChannels == 2) ? 16000 : 10000;
        if (iTune < iMin) iTune = iMin;
    }
    if (iTune < 8000 * iChannels) iTune = 8000 * iChannels;

    ixheaace_input_config* in = new ixheaace_input_config;
    ixheaace_output_config* out = new ixheaace_output_config;
    memset(in, 0, sizeof(*in));
    memset(out, 0, sizeof(*out));
    drcCfg.assign(1 << 20, 0); /* ia_drc_input_config (USAC only, but the API reads it) */
    in->pv_drc_cfg = drcCfg.data();
    in->aac_config.bitrate = iTune;
    in->aac_config.use_tns = 1;
    /* bits a frame may save for later ones: half an audio super frame (400 ms); the
       transmitter's frame size ranges keep every super frame exact */
    {
        int iRes = int((long(iNewBitRate) * 4 / 10) / 8 / 2 / iChannels);
        if (iRes < 64) iRes = 64;
        in->aac_config.bitreservoir_size = iRes;
    }
    in->i_bitrate = iTune;
    in->aot = iAot;
    in->codec_mode = 1;
    in->i_channels = iChannels;
    in->i_samp_freq = UWORD32(iInputRate);
    in->ui_pcm_wd_sz = 16;
    in->i_mps_tree_config = -1;
    in->i_use_adts = 0;
    in->i_use_es = 0;
    in->ccfl_idx = 1;
    in->frame_length = 960;
    in->frame_cmd_flag = 1;
    in->out_bytes_flag = 1;
    in->user_tns_flag = 1;
    in->random_access_interval = -1;
    in->aac_config.length = 0x7FFFFFFF;
    in->drm_mode = 1;
    out->malloc_xheaace = xaacMalloc;
    out->free_xheaace = xaacFree;

    const int32_t err = xaac_create(in, out);
    if (err != 0 || in->aot != iAot || in->drm_mode != 1) {
        cerr << "xaac: cannot create encoder (AOT " << iAot << ", " << iChannels << " ch, "
             << iInputRate << " Hz, " << iTune << " bit/s): error 0x" << hex << err << dec
             << (in->aot != iAot ? " (AOT changed)" : "") << endl;
        if ((err & IA_FATAL_ERROR) == 0)
            xaac_delete(out);
        delete in;
        delete out;
        return false;
    }
    pInCfg = in;
    pOutCfg = out;
    pEnc = out->pv_ia_process_api_obj;
    iBitRate = iNewBitRate;
    return true;
}

void XaacCodec::Destroy()
{
    if (pOutCfg != nullptr) {
        xaac_delete(pOutCfg);
        delete static_cast<ixheaace_output_config*>(pOutCfg);
        pOutCfg = nullptr;
    }
    if (pInCfg != nullptr) {
        delete static_cast<ixheaace_input_config*>(pInCfg);
        pInCfg = nullptr;
    }
    pEnc = nullptr;
}

int XaacCodec::Encode(CVector<_SAMPLE>& vecsEncInData, unsigned long lNumSampEncIn,
                      CVector<uint8_t>& vecsEncOutData, unsigned long lMaxBytesEncOut)
{
    if (pEnc == nullptr)
        return 0;
    ixheaace_input_config* in = static_cast<ixheaace_input_config*>(pInCfg);
    ixheaace_output_config* out = static_cast<ixheaace_output_config*>(pOutCfg);
    const unsigned long lIn = static_cast<unsigned long>(out->input_size) / sizeof(int16_t);
    int16_t* pIn = static_cast<int16_t*>(out->mem_info_table[IA_MEMTYPE_INPUT].mem_ptr);
    for (unsigned long i = 0; i < lIn; i++)
        pIn[i] = (i < lNumSampEncIn) ? int16_t(vecsEncInData[int(i)]) : int16_t(0);

    in->drm_frame_bytes = WORD32(lNextFrameBytes);
    in->drm_frame_bytes_min = WORD32(lNextFrameBytesMin);
    const int32_t err = xaac_process(pEnc, in, out);
    if (err & IA_FATAL_ERROR) {
        cerr << "xaac: encoder error 0x" << hex << err << dec << " (frame of "
             << lNextFrameBytes << " bytes)" << endl;
        /* the encoder state is undefined after a fatal error: start again */
        Create(iBitRate);
        return 0;
    }
    const int bytes = out->i_out_bytes;
    if (bytes <= 0)
        return 0;
    if (unsigned(bytes) > lMaxBytesEncOut || bytes > vecsEncOutData.Size()) {
        cerr << "xaac: output buffer too small" << endl;
        return 0;
    }
    const uint8_t* pOut = static_cast<const uint8_t*>(out->mem_info_table[IA_MEMTYPE_OUTPUT].mem_ptr);
    for (int i = 0; i < bytes; i++)
        vecsEncOutData[i] = pOut[i];
    return bytes;
}

void XaacCodec::EncClose()
{
    Destroy();
}

void XaacCodec::EncSetBitrate(int iNewBitRate)
{
    /* DREAM's rate is the audio payload without the CRC byte; libxaac only uses it to
       choose its tuning, so recreate the encoder only if it changes */
    if (pEnc != nullptr && iNewBitRate > 0 && iNewBitRate != iBitRate)
        Create(iNewBitRate);
}

void XaacCodec::EncUpdate(CAudioParam&)
{
}

void XaacCodec::EncSetNextFrameSize(unsigned long lBytes)
{
    lNextFrameBytes = lNextFrameBytesMin = lBytes;
}

void XaacCodec::EncSetNextFrameSizeRange(unsigned long lMinBytes, unsigned long lMaxBytes)
{
    lNextFrameBytesMin = lMinBytes;
    lNextFrameBytes = lMaxBytes;
}

string XaacCodec::fileName(const CParameter& Parameters) const
{
    stringstream ss;
    const CAudioParam& audioParam =
        Parameters.Service[unsigned(Parameters.GetCurSelAudioService())].AudioParam;
    ss << "aac_xaac_" << (audioParam.eAudioSamplRate == CAudioParam::AS_12KHZ ? "12kHz_" : "24kHz_");
    switch (audioParam.eAudioMode) {
    case CAudioParam::AM_MONO: ss << "mono"; break;
    case CAudioParam::AM_P_STEREO: ss << "pstereo"; break;
    case CAudioParam::AM_STEREO: ss << "stereo"; break;
    case CAudioParam::AM_RESERVED: break;
    }
    if (audioParam.eSBRFlag == CAudioParam::SB_USED)
        ss << "_sbr";
    ss << ".dat";
    return ss.str();
}
