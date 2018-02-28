/*
 *      Copyright (C) 2010-2017 Hendrik Leppkes
 *      http://www.1f0.de
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include "stdafx.h"
#include "DecodeManager.h"

#include "FMS30Video.h"

#include <Shlwapi.h>


CDecodeManager::CDecodeManager(CFMS30Video *pLAVVideo)
  : m_pFMSVideo(pLAVVideo)
{
  WCHAR fileName[1024];
  GetModuleFileName(nullptr, fileName, 1024);
  m_processName = PathFindFileName (fileName);
}

CDecodeManager::~CDecodeManager(void)
{
  Close();
}

STDMETHODIMP CDecodeManager::Close()
{
  CAutoLock decoderLock(this);
  SAFE_DELETE(m_pDecoder);
  
  return S_OK;
}

#define HWFORMAT_ENABLED \
   ((codec == AV_CODEC_ID_H264 && m_pFMSVideo->GetHWAccelCodec(HWCodec_H264))                                                       \
|| ((codec == AV_CODEC_ID_VC1 || codec == AV_CODEC_ID_WMV3) && m_pFMSVideo->GetHWAccelCodec(HWCodec_VC1))                           \
|| ((codec == AV_CODEC_ID_MPEG2VIDEO || codec == AV_CODEC_ID_MPEG1VIDEO) && m_pFMSVideo->GetHWAccelCodec(HWCodec_MPEG2) && (!(m_pFMSVideo->GetDecodeFlags() & LAV_VIDEO_DEC_FLAG_DVD) || m_pFMSVideo->GetHWAccelCodec(HWCodec_MPEG2DVD)))            \
|| (codec == AV_CODEC_ID_MPEG4 && m_pFMSVideo->GetHWAccelCodec(HWCodec_MPEG4)) \
|| (codec == AV_CODEC_ID_HEVC && m_pFMSVideo->GetHWAccelCodec(HWCodec_HEVC)) \
|| (codec == AV_CODEC_ID_VP9 && m_pFMSVideo->GetHWAccelCodec(HWCodec_VP9)))

#define HWRESOLUTION_ENABLED \
  ((pBMI->biHeight <= 576 && pBMI->biWidth <= 1024 && m_pFMSVideo->GetHWAccelResolutionFlags() & LAVHWResFlag_SD)     \
|| ((pBMI->biHeight > 576 || pBMI->biWidth > 1024) && pBMI->biHeight <= 1200 && pBMI->biWidth <= 1920 && m_pFMSVideo->GetHWAccelResolutionFlags() & LAVHWResFlag_HD)    \
|| ((pBMI->biHeight > 1200 || pBMI->biWidth > 1920) && m_pFMSVideo->GetHWAccelResolutionFlags() & LAVHWResFlag_UHD))

STDMETHODIMP CDecodeManager::CreateDecoder(const CMediaType *pmt, AVCodecID codec)
{
  CAutoLock decoderLock(this);

  DbgLog((LOG_TRACE, 10, L"CDecodeThread::CreateDecoder(): Creating new decoder for codec %S", avcodec_get_name(codec)));
  HRESULT hr = S_OK;
  BOOL bWMV9 = FALSE;

  BITMAPINFOHEADER *pBMI = nullptr;
  videoFormatTypeHandler(*pmt, &pBMI);

  SAFE_DELETE(m_pDecoder);

//softwaredec:
  // Fallback for software
  if (!m_pDecoder) {
    DbgLog((LOG_TRACE, 10, L"-> No HW Codec, using Software"));
    m_bHWDecoder = FALSE;
	m_pDecoder = CreateDecoderAVCodec();

  }
  DbgLog((LOG_TRACE, 10, L"-> Created Codec '%s'", m_pDecoder->GetDecoderName()));

  hr = m_pDecoder->InitInterfaces(static_cast<ILAVVideoSettings *>(m_pFMSVideo), static_cast<ILAVVideoCallback *>(m_pFMSVideo));
  if (FAILED(hr)) {
    DbgLog((LOG_TRACE, 10, L"-> Init Interfaces failed (hr: 0x%x)", hr));
    goto done;
  }

  hr = m_pDecoder->InitDecoder(codec, pmt);
  if (FAILED(hr)) {
    DbgLog((LOG_TRACE, 10, L"-> Init Decoder failed (hr: 0x%x)", hr));
    goto done;
  }

done:
  if (FAILED(hr)) {
    SAFE_DELETE(m_pDecoder);
    return hr;
  }

  m_Codec = codec;

  return hr;
}

STDMETHODIMP CDecodeManager::Decode(IMediaSample *pSample)
{
  CAutoLock decoderLock(this);
  HRESULT hr = S_OK;

  if (!m_pDecoder)
    return E_UNEXPECTED;

  hr = m_pDecoder->Decode(pSample);

  // If a hardware decoder indicates a hard failure, we switch back to software
  // This is used to indicate incompatible media
  if (FAILED(hr) && m_bHWDecoder) {
    DbgLog((LOG_TRACE, 10, L"CDecodeManager::Decode(): Hardware decoder indicates failure, switching back to software"));
    m_bHWDecoderFailed = TRUE;

    // If we're disabling DXVA2 Native decoding, we need to release resources now
    if (wcscmp(m_pDecoder->GetDecoderName(), L"dxva2n") == 0 || wcscmp(m_pDecoder->GetDecoderName(), L"d3d11 native") == 0) {
      m_pFMSVideo->ReleaseAllDXVAResources();
      m_pFMSVideo->GetOutputPin()->GetConnected()->BeginFlush();
      m_pFMSVideo->GetOutputPin()->GetConnected()->EndFlush();

      // TODO: further decoding still fails when DXVA2-Native fails mid-decoding, since we can't inform the renderer about no longer delivering DXVA surfaces
    }

    CMediaType &mt = m_pFMSVideo->GetInputMediaType();
    hr = CreateDecoder(&mt, m_Codec);

    if (SUCCEEDED(hr)) {
      hr = m_pDecoder->Decode(pSample);
    }
  }

  return S_OK;
}

STDMETHODIMP CDecodeManager::Flush()
{
  CAutoLock decoderLock(this);

  if (!m_pDecoder)
    return E_UNEXPECTED;

  return m_pDecoder->Flush();
}

STDMETHODIMP CDecodeManager::EndOfStream()
{
  CAutoLock decoderLock(this);

  if (!m_pDecoder)
    return E_UNEXPECTED;

  return m_pDecoder->EndOfStream();
}

STDMETHODIMP CDecodeManager::InitAllocator(IMemAllocator **ppAlloc)
{
  CAutoLock decoderLock(this);

  if (!m_pDecoder)
    return E_UNEXPECTED;

  return m_pDecoder->InitAllocator(ppAlloc);
}

STDMETHODIMP CDecodeManager::PostConnect(IPin *pPin)
{
  CAutoLock decoderLock(this);
  HRESULT hr = S_OK;
  if (m_pDecoder) {
    hr = m_pDecoder->PostConnect(pPin);
    if (FAILED(hr)) {
      m_bHWDecoderFailed = TRUE;
      CMediaType &mt = m_pFMSVideo->GetInputMediaType();
      hr = CreateDecoder(&mt, m_Codec);
    }
  }
  return hr;
}

STDMETHODIMP CDecodeManager::BreakConnect()
{
  CAutoLock decoderLock(this);

  if (!m_pDecoder)
    return E_UNEXPECTED;

  return m_pDecoder->BreakConnect();
}
