#pragma once
#include <initguid.h>
#include <combaseapi.h>

DEFINE_GUID(IID_ILAVAudioSettings, 
0x4158a22b, 0x6553, 0x45d0, 0x80, 0x69, 0x24, 0x71, 0x6f, 0x8f, 0xf1, 0x71);

// Codecs supported in the LAV Audio configuration
// Codecs not listed here cannot be turned off. You can request codecs to be added to this list, if you wish.
typedef enum LAVAudioCodec {
  Codec_AAC,
  Codec_AC3,
  Codec_EAC3,
  Codec_DTS,
  Codec_MP2,
  Codec_MP3,
  Codec_TRUEHD,
  Codec_FLAC,
  Codec_VORBIS,
  Codec_LPCM,
  Codec_PCM,
  Codec_WAVPACK,
  Codec_TTA,
  Codec_WMA2,
  Codec_WMAPRO,
  Codec_Cook,
  Codec_RealAudio,
  Codec_WMALL,
  Codec_ALAC,
  Codec_Opus,
  Codec_AMR,
  Codec_Nellymoser,
  Codec_MSPCM,
  Codec_Truespeech,
  Codec_TAK,
  Codec_ATRAC,

  Codec_AudioNB            // Number of entries (do not use when dynamically linking)
} LAVAudioCodec;

// Bitstreaming Codecs supported in LAV Audio
typedef enum LAVBitstreamCodec {
  Bitstream_AC3,
  Bitstream_EAC3,
  Bitstream_TRUEHD,
  Bitstream_DTS,
  Bitstream_DTSHD,

  Bitstream_NB        // Number of entries (do not use when dynamically linking)
} LAVBitstreamCodec;


// Supported Sample Formats in LAV Audio
typedef enum LAVAudioSampleFormat {
  SampleFormat_None = -1,
  SampleFormat_16,
  SampleFormat_24,
  SampleFormat_32,
  SampleFormat_U8,
  SampleFormat_FP32,
  SampleFormat_Bitstream,

  SampleFormat_NB     // Number of entries (do not use when dynamically linking)
} LAVAudioSampleFormat;

typedef enum LAVAudioMixingMode {
  MatrixEncoding_None,
  MatrixEncoding_Dolby,
  MatrixEncoding_DPLII,

  MatrixEncoding_NB
} LAVAudioMixingMode;

// LAV Audio configuration interface
interface __declspec(uuid("4158A22B-6553-45D0-8069-24716F8FF171")) ILAVAudioSettings : public IUnknown
{
	// Convert all Channel Layouts to standard layouts
	// Standard are: Mono, Stereo, 5.1, 6.1, 7.1
	STDMETHOD_(BOOL, GetOutputStandardLayout)() = 0;
	STDMETHOD(SetOutputStandardLayout)(BOOL bStdLayout) = 0;
};
