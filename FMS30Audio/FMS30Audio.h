#pragma once

#include "AudioSettings.h"
#include "FloatingAverage.h"
#include "Media.h"


// Buffer Size for decoded PCM: 1s of 192kHz 32-bit with 8 channels
// 192000 (Samples) * 4 (Bytes per Sample) * 8 (channels)
#define FMS_AUDIO_BUFFER_SIZE 6144000

// Maximum Durations (in reference time)
// 10ms (DTS has 10.6667 ms samples, don't want to queue them up)
#define PCM_BUFFER_MAX_DURATION 100000
// 6ms
#define PCM_BUFFER_MIN_DURATION 60000

struct BufferDetails {
	GrowableArray<BYTE>   *bBuffer = nullptr;         // PCM Buffer
	LAVAudioSampleFormat  sfFormat = SampleFormat_16; // Sample Format
	WORD                  wBitsPerSample = 0;               // Bits per sample
	DWORD                 dwSamplesPerSec = 0;               // Samples per second
	unsigned              nSamples = 0;               // Samples in the buffer (every sample is sizeof(sfFormat) * nChannels in the buffer)
	WORD                  wChannels = 0;               // Number of channels
	DWORD                 dwChannelMask = 0;               // channel mask
	REFERENCE_TIME        rtStart = AV_NOPTS_VALUE;  // Start Time of the buffer
	BOOL                  bPlanar = FALSE;           // Planar (not used)


	BufferDetails() {
		bBuffer = new GrowableArray<BYTE>();
	};
	~BufferDetails() {
		delete bBuffer;
	}
};

class __declspec(uuid("1638AF29-DFBF-4627-833B-FDE9B018FDD0")) CFMS30Audio:public CTransformFilter ,ILAVAudioSettings
{
public:
	CFMS30Audio(LPUNKNOWN pUnk,HRESULT* phr);
	virtual ~CFMS30Audio();

	static void CALLBACK StaticInit(BOOL bLoading, const CLSID *clsid);

	// IUnknown
	DECLARE_IUNKNOWN;
	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

	//audioSettings
	STDMETHODIMP_(BOOL) GetOutputStandardLayout();
	STDMETHODIMP SetOutputStandardLayout(BOOL bStdLayout);

	// CTransformFilter
	HRESULT CheckInputType(const CMediaType* mtIn);
	HRESULT CheckTransform(const CMediaType* mtIn, const CMediaType* mtOut);
	HRESULT DecideBufferSize(IMemAllocator * pAllocator, ALLOCATOR_PROPERTIES *pprop);
	HRESULT GetMediaType(int iPosition, CMediaType *pMediaType);

	HRESULT Receive(IMediaSample *pIn);

	STDMETHODIMP JoinFilterGraph(IFilterGraph * pGraph, LPCWSTR pName);

	// Optional Overrides
	HRESULT CheckConnect(PIN_DIRECTION dir, IPin *pPin);
	HRESULT SetMediaType(PIN_DIRECTION dir, const CMediaType *pmt);

	HRESULT EndOfStream();
	HRESULT BeginFlush();
	HRESULT EndFlush();
	HRESULT NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate);

	HRESULT BreakConnect(PIN_DIRECTION Dir);

public:
	// Pin Configuration
	const static AMOVIESETUP_MEDIATYPE    sudPinTypesIn[];
	const static UINT                     sudPinTypesInCount;
	const static AMOVIESETUP_MEDIATYPE    sudPinTypesOut[];
	const static UINT                     sudPinTypesOutCount;

private:
	HRESULT ffmpeg_init(AVCodecID codec, const void *format, GUID format_type, DWORD formatlen);
	void ffmpeg_shutdown();

	CMediaType CreateMediaType(LAVAudioSampleFormat outputFormat, DWORD nSamplesPerSec, WORD nChannels, DWORD dwChannelMask, WORD wBitsPerSample = 0) const;
	HRESULT ProcessBuffer(IMediaSample *pMediaSample, BOOL bEOF = FALSE);
	HRESULT Decode(const BYTE *p, int buffsize, int &consumed, HRESULT *hrDeliver, IMediaSample *pMediaSample);
	HRESULT PostProcess(BufferDetails *buffer);
	HRESULT GetDeliveryBuffer(IMediaSample **pSample, BYTE **pData);

	HRESULT QueueOutput(BufferDetails &buffer);
	HRESULT FlushOutput(BOOL bDeliver = TRUE);
	HRESULT FlushDecoder();


	HRESULT PerformFlush();
	HRESULT Deliver(BufferDetails &buffer);

	LAVAudioSampleFormat GetBestAvailableSampleFormat(LAVAudioSampleFormat inFormat, int *bits = NULL, BOOL bNoFallback = FALSE);

private:
	AVCodecID             m_nCodecId = AV_CODEC_ID_NONE;
	AVCodec              *m_pAVCodec = nullptr;
	AVCodecContext       *m_pAVCtx = nullptr;
	AVCodecParserContext *m_pParser = nullptr;
	AVFrame              *m_pFrame = nullptr;
	SwrContext			 *m_pSwr = nullptr;

	BOOL                 m_bFlushing = FALSE;
	BOOL                 m_bDiscontinuity = FALSE;
	REFERENCE_TIME       m_rtStart = 0;
	double               m_dStartOffset = 0.0;
	double               m_dRate = 1.0;

	REFERENCE_TIME       m_rtStartInput = AV_NOPTS_VALUE;   // rtStart of the current input package
	REFERENCE_TIME       m_rtStopInput = AV_NOPTS_VALUE;   // rtStop of the current input package
	REFERENCE_TIME       m_rtStartInputCache = AV_NOPTS_VALUE;   // rtStart of the last input package
	REFERENCE_TIME       m_rtStopInputCache = AV_NOPTS_VALUE;   // rtStop of the last input package
	REFERENCE_TIME       m_rtBitstreamCache = AV_NOPTS_VALUE;   // Bitstreaming time cache
	BOOL                 m_bUpdateTimeCache = TRUE;

	GrowableArray<BYTE>  m_buff;                                 // Input Buffer
	LAVAudioSampleFormat m_DecodeFormat = SampleFormat_16;

private:
	// Settings
	struct AudioSettings {
		BOOL OutputStandardLayout;
	} m_settings;

	BOOL                m_bQueueResync = FALSE;
	BOOL                m_bResyncTimestamp = FALSE;
	BOOL                m_bNeedSyncpoint = FALSE;
	BOOL                m_bJustFlushed = TRUE;
	BufferDetails       m_OutputQueue;

	DWORD               m_DecodeLayout = 0;

};

