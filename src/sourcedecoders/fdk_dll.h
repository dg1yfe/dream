/******************************************************************************\
 *
 * Description:
 *  Runtime loading of the Fraunhofer FDK AAC library (libfdk-aac).
 *
 *  The FDK AAC licence is not GPL compatible, so DREAM (GPLv2) is never linked
 *  against libfdk-aac: it is compiled against the fdk-aac HEADERS only (types,
 *  enums, the inline FDKinitLibInfo()/FDKlibInfo_getCapabilities() helpers)
 *  and every library function is resolved with dlsym()/GetProcAddress() via
 *  CLibraryLoader, like libfaad_drm / libfaac_drm / libopus. Users who want
 *  FDK support install the library themselves; without it FdkAacCodec reports
 *  CanDecode()/CanEncode() false and DREAM falls back to faad/faac.
 *
 *  Environment:
 *    DREAM_NO_FDK=1     never load the library (test the faad/faac fallback)
 *    DREAM_LIB_PATH     extra directories searched (macOS, see LibraryLoader.h)
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

#ifndef FDK_DLL_H_
#define FDK_DLL_H_

#include <fdk-aac/aacdecoder_lib.h>
#include <fdk-aac/aacenc_lib.h>
#include <string>

/* Signatures of the libfdk-aac functions DREAM uses (as in fdk-aac 2.0.x). */
typedef INT               (fdk_aacDecoder_GetLibInfo_t)(LIB_INFO*);
typedef HANDLE_AACDECODER (fdk_aacDecoder_Open_t)(TRANSPORT_TYPE, UINT);
typedef AAC_DECODER_ERROR (fdk_aacDecoder_ConfigRaw_t)(HANDLE_AACDECODER, UCHAR*[], const UINT[]);
typedef CStreamInfo*      (fdk_aacDecoder_GetStreamInfo_t)(HANDLE_AACDECODER);
typedef AAC_DECODER_ERROR (fdk_aacDecoder_Fill_t)(HANDLE_AACDECODER, UCHAR*[], const UINT[], UINT*);
typedef AAC_DECODER_ERROR (fdk_aacDecoder_DecodeFrame_t)(HANDLE_AACDECODER, INT_PCM*, const INT, const UINT);
typedef void              (fdk_aacDecoder_Close_t)(HANDLE_AACDECODER);
typedef AACENC_ERROR      (fdk_aacEncGetLibInfo_t)(LIB_INFO*);
typedef AACENC_ERROR      (fdk_aacEncOpen_t)(HANDLE_AACENCODER*, const UINT, const UINT);
typedef AACENC_ERROR      (fdk_aacEncoder_SetParam_t)(const HANDLE_AACENCODER, const AACENC_PARAM, const UINT);
typedef AACENC_ERROR      (fdk_aacEncEncode_t)(const HANDLE_AACENCODER, const AACENC_BufDesc*, const AACENC_BufDesc*,
                                               const AACENC_InArgs*, AACENC_OutArgs*);
typedef AACENC_ERROR      (fdk_aacEncInfo_t)(const HANDLE_AACENCODER, AACENC_InfoStruct*);
typedef AACENC_ERROR      (fdk_aacEncClose_t)(HANDLE_AACENCODER*);

/* Load libfdk-aac once (thread safe); true if every function was resolved.
   Logs which library file was loaded, or why none was. */
bool FdkLibLoad();
/* Path of the loaded library ("" if none). */
std::string FdkLibPath();

#endif // FDK_DLL_H_
