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

#pragma once

// {FA40D6E9-4D38-4761-ADD2-71A9EC5FD32F}
DEFINE_GUID(IID_ILAVVideoSettings, 
0xfa40d6e9, 0x4d38, 0x4761, 0xad, 0xd2, 0x71, 0xa9, 0xec, 0x5f, 0xd3, 0x2f);

// {1CC2385F-36FA-41B1-9942-5024CE0235DC}
DEFINE_GUID(IID_ILAVVideoStatus,
0x1cc2385f, 0x36fa, 0x41b1, 0x99, 0x42, 0x50, 0x24, 0xce, 0x2, 0x35, 0xdc);


// Codecs supported in the LAV Video configuration
// Codecs not listed here cannot be turned off. You can request codecs to be added to this list, if you wish.
typedef enum LAVVideoCodec {
  Codec_H264,
  Codec_VC1,
  Codec_MPEG1,
  Codec_MPEG2,
  Codec_MPEG4,
  Codec_MSMPEG4,
  Codec_VP8,
  Codec_WMV3,
  Codec_WMV12,
  Codec_MJPEG,
  Codec_Theora,
  Codec_FLV1,
  Codec_VP6,
  Codec_SVQ,
  Codec_H261,
  Codec_H263,
  Codec_Indeo,
  Codec_TSCC,
  Codec_Fraps,
  Codec_HuffYUV,
  Codec_QTRle,
  Codec_DV,
  Codec_Bink,
  Codec_Smacker,
  Codec_RV12,
  Codec_RV34,
  Codec_Lagarith,
  Codec_Cinepak,
  Codec_Camstudio,
  Codec_QPEG,
  Codec_ZLIB,
  Codec_QTRpza,
  Codec_PNG,
  Codec_MSRLE,
  Codec_ProRes,
  Codec_UtVideo,
  Codec_Dirac,
  Codec_DNxHD,
  Codec_MSVideo1,
  Codec_8BPS,
  Codec_LOCO,
  Codec_ZMBV,
  Codec_VCR1,
  Codec_Snow,
  Codec_FFV1,
  Codec_v210,
  Codec_JPEG2000,
  Codec_VMNC,
  Codec_FLIC,
  Codec_G2M,
  Codec_ICOD,
  Codec_THP,
  Codec_HEVC,
  Codec_VP9,
  Codec_TrueMotion,
  Codec_VP7,
  Codec_H264MVC,
  Codec_CineformHD,
  Codec_MagicYUV,

  Codec_VideoNB            // Number of entries (do not use when dynamically linking)
} LAVVideoCodec;

// Codecs with hardware acceleration
typedef enum LAVVideoHWCodec {
  HWCodec_H264  = Codec_H264,
  HWCodec_VC1   = Codec_VC1,
  HWCodec_MPEG2 = Codec_MPEG2,
  HWCodec_MPEG4 = Codec_MPEG4,
  HWCodec_MPEG2DVD,
  HWCodec_HEVC,
  HWCodec_VP9,

  HWCodec_NB,
} LAVVideoHWCodec;

// Flags for HW Resolution support
#define LAVHWResFlag_SD      0x0001
#define LAVHWResFlag_HD      0x0002
#define LAVHWResFlag_UHD     0x0004

// Type of hardware accelerations
typedef enum LAVHWAccel {
  HWAccel_None,
  HWAccel_CUDA,
  HWAccel_QuickSync,
  HWAccel_DXVA2,
  HWAccel_DXVA2CopyBack = HWAccel_DXVA2,
  HWAccel_DXVA2Native,
  HWAccel_D3D11,
  HWAccel_NB,              // Number of HWAccels
} LAVHWAccel;

// Deinterlace algorithms offered by the hardware decoders
typedef enum LAVHWDeintModes {
  HWDeintMode_Weave,
  HWDeintMode_BOB, // Deprecated
  HWDeintMode_Hardware
} LAVHWDeintModes;

// Software deinterlacing algorithms
typedef enum LAVSWDeintModes {
  SWDeintMode_None,
  SWDeintMode_YADIF,
  SWDeintMode_W3FDIF_Simple,
  SWDeintMode_W3FDIF_Complex,
} LAVSWDeintModes;

// Deinterlacing processing mode
typedef enum LAVDeintMode {
  DeintMode_Auto,
  DeintMode_Aggressive,
  DeintMode_Force,
  DeintMode_Disable
} LAVDeintMode;

// Type of deinterlacing to perform
// - FramePerField re-constructs one frame from every field, resulting in 50/60 fps.
// - FramePer2Field re-constructs one frame from every 2 fields, resulting in 25/30 fps.
// Note: Weave will always use FramePer2Field
typedef enum LAVDeintOutput {
  DeintOutput_FramePerField,
  DeintOutput_FramePer2Field
} LAVDeintOutput;

// Control the field order of the deinterlacer
typedef enum LAVDeintFieldOrder {
  DeintFieldOrder_Auto,
  DeintFieldOrder_TopFieldFirst,
  DeintFieldOrder_BottomFieldFirst,
} LAVDeintFieldOrder;

// Supported output pixel formats
typedef enum LAVOutPixFmts {
  LAVOutPixFmt_None = -1,
  LAVOutPixFmt_YV12,            // 4:2:0, 8bit, planar
  LAVOutPixFmt_NV12,            // 4:2:0, 8bit, Y planar, U/V packed
  LAVOutPixFmt_YUY2,            // 4:2:2, 8bit, packed
  LAVOutPixFmt_UYVY,            // 4:2:2, 8bit, packed
  LAVOutPixFmt_AYUV,            // 4:4:4, 8bit, packed
  LAVOutPixFmt_P010,            // 4:2:0, 10bit, Y planar, U/V packed
  LAVOutPixFmt_P210,            // 4:2:2, 10bit, Y planar, U/V packed
  LAVOutPixFmt_Y410,            // 4:4:4, 10bit, packed
  LAVOutPixFmt_P016,            // 4:2:0, 16bit, Y planar, U/V packed
  LAVOutPixFmt_P216,            // 4:2:2, 16bit, Y planar, U/V packed
  LAVOutPixFmt_Y416,            // 4:4:4, 16bit, packed
  LAVOutPixFmt_RGB32,           // 32-bit RGB (BGRA)
  LAVOutPixFmt_RGB24,           // 24-bit RGB (BGR)

  LAVOutPixFmt_v210,            // 4:2:2, 10bit, packed
  LAVOutPixFmt_v410,            // 4:4:4, 10bit, packed

  LAVOutPixFmt_YV16,            // 4:2:2, 8-bit, planar
  LAVOutPixFmt_YV24,            // 4:4:4, 8-bit, planar

  LAVOutPixFmt_RGB48,           // 48-bit RGB (16-bit per pixel, BGR)

  LAVOutPixFmt_NB               // Number of formats
} LAVOutPixFmts;

typedef enum LAVDitherMode {
  LAVDither_Ordered,
  LAVDither_Random
} LAVDitherMode;

// LAV Video configuration interface
interface __declspec(uuid("FA40D6E9-4D38-4761-ADD2-71A9EC5FD32F")) ILAVVideoSettings : public IUnknown
{
  // Set the number of threads to use for Multi-Threaded decoding (where available)
  //  0 = Auto Detect (based on number of CPU cores)
  //  1 = 1 Thread -- No Multi-Threading
  // >1 = Multi-Threading with the specified number of threads
  STDMETHOD(SetNumThreads)(DWORD dwNum) = 0;

  // Get the number of threads to use for Multi-Threaded decoding (where available)
  //  0 = Auto Detect (based on number of CPU cores)
  //  1 = 1 Thread -- No Multi-Threading
  // >1 = Multi-Threading with the specified number of threads
  STDMETHOD_(DWORD,GetNumThreads)() = 0;

  // Configure which pixel formats are enabled for output
  // If pixFmt is invalid, Get will return FALSE and Set E_FAIL
  STDMETHOD_(BOOL,GetPixelFormat)(LAVOutPixFmts pixFmt) = 0;
  STDMETHOD(SetPixelFormat)(LAVOutPixFmts pixFmt, BOOL bEnabled) = 0;

  // Set the software deinterlacing mode used
  STDMETHOD(SetSWDeintMode)(LAVSWDeintModes deintMode) = 0;

  // Get the software deinterlacing mode used
  STDMETHOD_(LAVSWDeintModes, GetSWDeintMode)() = 0;

  // Set the software deinterlacing output
  STDMETHOD(SetSWDeintOutput)(LAVDeintOutput deintOutput) = 0;

  // Get the software deinterlacing output
  STDMETHOD_(LAVDeintOutput, GetSWDeintOutput)() = 0;

  // Set the dithering mode used
  STDMETHOD(SetDitherMode)(LAVDitherMode ditherMode) = 0;

  // Get the dithering mode used
  STDMETHOD_(LAVDitherMode, GetDitherMode)() = 0;

  // Set the Deint Mode
  STDMETHOD(SetDeinterlacingMode)(LAVDeintMode deintMode) = 0;

  // Get the Deint Mode
  STDMETHOD_(LAVDeintMode,GetDeinterlacingMode)() = 0;
};

// LAV Video status interface
 interface __declspec(uuid("1CC2385F-36FA-41B1-9942-5024CE0235DC")) ILAVVideoStatus : public IUnknown
{
  // Get the name of the active decoder (can return NULL if none is active)
  STDMETHOD_(LPCWSTR, GetActiveDecoderName)() = 0;

  // Get the name of the currently active hwaccel device
  STDMETHOD(GetHWAccelActiveDevice)(BSTR *pstrDeviceName) = 0;
};
