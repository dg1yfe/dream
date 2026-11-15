/******************************************************************************\
 *
 * Description:
 *  Runtime binding of libxaacenc_drm (Google/Ittiam libxaac AAC encoder,
 *  Apache-2.0, with DREAM's DRM patches from macx/patches/libxaac-*.patch).
 *  The library is dlopen()ed by XaacCodec; nothing here links against it.
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

#ifndef XAAC_DLL_H_
#define XAAC_DLL_H_

#include <cstdint>

/* ixheaace_api.h: IA_ERRORCODE f(pVOID, ...) with IA_ERRORCODE = WORD32 */
typedef int32_t xaac_get_lib_id_strings_t(void* pv_output);
typedef int32_t xaac_create_t(void* pv_input, void* pv_output);
typedef int32_t xaac_process_t(void* pv_api_obj, void* pv_input, void* pv_output);
typedef int32_t xaac_delete_t(void* pv_output);

#endif /* XAAC_DLL_H_ */
