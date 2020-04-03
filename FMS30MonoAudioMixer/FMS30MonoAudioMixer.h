// 下列 ifdef 块是创建使从 DLL 导出更简单的
// 宏的标准方法。此 DLL 中的所有文件都是用命令行上定义的 FMS30MONOAUDIOMIXER_EXPORTS
// 符号编译的。在使用此 DLL 的
// 任何其他项目上不应定义此符号。这样，源文件中包含此文件的任何其他项目都会将
// FMS30MONOAUDIOMIXER_API 函数视为是从 DLL 导入的，而此 DLL 则将用此宏定义的
// 符号视为是被导出的。
#ifdef FMS30MONOAUDIOMIXER_EXPORTS
#define FMS30MONOAUDIOMIXER_API __declspec(dllexport)
#else
#define FMS30MONOAUDIOMIXER_API __declspec(dllimport)
#endif

#include "AudioSettings.h"
#include "FloatingAverage.h"
#include "Media.h"
#include <vector>
#include "MonoAudioInputPin.h"

// {0863A64D-9CD0-4477-B1FD-FFE22D55F962}
DEFINE_GUID(FMS30MonoAudioMixerGuid,
	0x863a64d, 0x9cd0, 0x4477, 0xb1, 0xfd, 0xff, 0xe2, 0x2d, 0x55, 0xf9, 0x62);

// 此类是从 FMS30MonoAudioMixer.dll 导出的
class  __declspec(uuid("0863A64D-9CD0-4477-B1FD-FFE22D55F962"))  CFMS30MonoAudioMixer :public CTransformFilter, public IMonoAReceiveInterface
{
public:
	CFMS30MonoAudioMixer(LPUNKNOWN pUnk, HRESULT* phr);
	virtual ~CFMS30MonoAudioMixer();
	// TODO:  在此添加您的方法。

	// Pin Configuration
	const static AMOVIESETUP_MEDIATYPE    sudPinTypesIn[];
	const static UINT                     sudPinTypesInCount;
	const static AMOVIESETUP_MEDIATYPE    sudPinTypesOut[];
	const static UINT                     sudPinTypesOutCount;

	std::vector<CMonoAudioInputPin *> m_vInputPin;

	virtual HRESULT OnAudioReceive(IMediaSample * pSample, EnumAudioCHNPos apos);

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


	static void CALLBACK StaticInit(BOOL bLoading, const CLSID *clsid);

	// IUnknown
	DECLARE_IUNKNOWN;
	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

	virtual int GetPinCount();
	virtual CBasePin * GetPin(int n);

	// CTransformFilter
	HRESULT CheckInputType(const CMediaType* mtIn);
	HRESULT CheckTransform(const CMediaType* mtIn, const CMediaType* mtOut);
	HRESULT DecideBufferSize(IMemAllocator * pAllocator, ALLOCATOR_PROPERTIES *pprop);
	// override to suggest OUTPUT pin media types
	HRESULT GetMediaType(int iPosition, CMediaType *pMediaType);

	HRESULT Receive(IMediaSample *pIn);

	virtual HRESULT CompleteConnect(PIN_DIRECTION direction, IPin *pReceivePin);


	STDMETHODIMP JoinFilterGraph(IFilterGraph * pGraph, LPCWSTR pName);

	// Optional Overrides
	HRESULT CheckConnect(PIN_DIRECTION dir, IPin *pPin);
	HRESULT SetMediaType(PIN_DIRECTION dir, const CMediaType *pmt);

	HRESULT EndOfStream();
	HRESULT BeginFlush();
	HRESULT EndFlush();
	HRESULT NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate);

	HRESULT BreakConnect(PIN_DIRECTION Dir);

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

	CCritSec m_csPins;


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

	GrowableArray<BYTE>  m_buff;  // Input Left audio Buffer
	GrowableArray<BYTE>  m_buffR; // Input right audio Buffer
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

	CMediaType m_outputMediaType;
	WAVEFORMATEX m_wavFmt;
	HRESULT ProcessBufferLR();
};

