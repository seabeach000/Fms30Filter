// FMS30Video.cpp : 定义 DLL 应用程序的导出函数。
//

#pragma once

#include "decoders/ILAVDecoder.h"
#include "DecodeManager.h"
#include "ILAVPinInfo.h"

#include "LAVPixFmtConverter.h"

#include "ISpecifyPropertyPages2.h"

class CDeCSSTransformInputPin;

extern "C" {
#include "libavutil/mastering_display_metadata.h"
};

class __declspec(uuid("94FDB89F-B604-48C2-B785-105B7F781F76")) CFMS30Video :public CTransformFilter , public ISpecifyPropertyPages2,public ILAVVideoCallback,public ILAVVideoSettings
{
public:
	CFMS30Video(LPUNKNOWN pUnk, HRESULT* phr);
	~CFMS30Video();

	static void CALLBACK StaticInit(BOOL bLoading, const CLSID *clsid);

	// IUnknown
	DECLARE_IUNKNOWN;
	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

	// ILAVVideoSettings
	STDMETHODIMP SetNumThreads(DWORD dwNum);
	STDMETHODIMP_(DWORD) GetNumThreads();

	STDMETHODIMP SetPixelFormat(LAVOutPixFmts pixFmt, BOOL bEnabled);
	STDMETHODIMP_(BOOL) GetPixelFormat(LAVOutPixFmts pixFmt);

	STDMETHODIMP SetSWDeintOutput(LAVDeintOutput deintOutput);
	STDMETHODIMP_(LAVDeintOutput) GetSWDeintOutput();

	STDMETHODIMP SetDitherMode(LAVDitherMode ditherMode);
	STDMETHODIMP_(LAVDitherMode) GetDitherMode();

	STDMETHODIMP SetSWDeintMode(LAVSWDeintModes deintMode);
	STDMETHODIMP_(LAVSWDeintModes) GetSWDeintMode();

	STDMETHODIMP SetDeinterlacingMode(LAVDeintMode deintMode);
	STDMETHODIMP_(LAVDeintMode) GetDeinterlacingMode();

	// CTransformFilter
	STDMETHODIMP Stop();

	HRESULT CheckInputType(const CMediaType* mtIn);
	HRESULT CheckTransform(const CMediaType* mtIn, const CMediaType* mtOut);
	HRESULT DecideBufferSize(IMemAllocator * pAllocator, ALLOCATOR_PROPERTIES *pprop);
	HRESULT GetMediaType(int iPosition, CMediaType *pMediaType);

	HRESULT SetMediaType(PIN_DIRECTION dir, const CMediaType *pmt);
	HRESULT EndOfStream();
	HRESULT BeginFlush();
	HRESULT EndFlush();
	HRESULT NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate);
	HRESULT Receive(IMediaSample *pIn);

	HRESULT CheckConnect(PIN_DIRECTION dir, IPin *pPin);
	HRESULT BreakConnect(PIN_DIRECTION dir);
	HRESULT CompleteConnect(PIN_DIRECTION dir, IPin *pReceivePin);

	int GetPinCount();
	CBasePin* GetPin(int n);

	STDMETHODIMP JoinFilterGraph(IFilterGraph * pGraph, LPCWSTR pName);

	// IPinSegmentEx
	HRESULT EndOfSegment();

	// ILAVVideoCallback
	STDMETHODIMP AllocateFrame(LAVFrame **ppFrame);
	STDMETHODIMP ReleaseFrame(LAVFrame **ppFrame);
	STDMETHODIMP Deliver(LAVFrame *pFrame);
	STDMETHODIMP_(LPWSTR) GetFileExtension();
	STDMETHODIMP_(BOOL) FilterInGraph(PIN_DIRECTION dir, const GUID &clsid) { if (dir == PINDIR_INPUT) return FilterInGraphSafe(m_pInput, clsid); else return FilterInGraphSafe(m_pOutput, clsid); }
	STDMETHODIMP_(DWORD) GetDecodeFlags() { return m_dwDecodeFlags; }
	STDMETHODIMP_(CMediaType&) GetInputMediaType() { return m_pInput->CurrentMediaType(); }
	STDMETHODIMP GetLAVPinInfo(LAVPinInfo &info) { if (m_LAVPinInfoValid) { info = m_LAVPinInfo; return S_OK; } return E_FAIL; }
	STDMETHODIMP_(CBasePin*) GetOutputPin() { return m_pOutput; }
	STDMETHODIMP_(CMediaType&) GetOutputMediaType() { return m_pOutput->CurrentMediaType(); }
	STDMETHODIMP DVDStripPacket(BYTE*& p, long& len) { /*static_cast<CDeCSSTransformInputPin*>(m_pInput)->StripPacket(p, len);*/ return S_OK; }
	STDMETHODIMP_(LAVFrame*) GetFlushFrame();
	STDMETHODIMP ReleaseAllDXVAResources() { ReleaseLastSequenceFrame(); return S_OK; }
	STDMETHODIMP_(DWORD) GetGPUDeviceIndex() { return m_dwGPUDeviceIndex; }
	STDMETHODIMP_(BOOL) HasDynamicInputAllocator();

public:
	// Pin Configuration
	const static AMOVIESETUP_MEDIATYPE    sudPinTypesIn[];
	const static UINT                     sudPinTypesInCount;
	const static AMOVIESETUP_MEDIATYPE    sudPinTypesOut[];
	const static UINT                     sudPinTypesOutCount;

private:
	HRESULT LoadDefaults();
	HRESULT LoadSettings();


	HRESULT CreateDecoder(const CMediaType *pmt);


	HRESULT GetDeliveryBuffer(IMediaSample** ppOut, int width, int height, AVRational ar, DXVA2_ExtendedFormat dxvaExtFormat, REFERENCE_TIME avgFrameDuration);
	HRESULT ReconnectOutput(int width, int height, AVRational ar, DXVA2_ExtendedFormat dxvaExtFlags, REFERENCE_TIME avgFrameDuration, BOOL bDXVA = FALSE);

	HRESULT SetFrameFlags(IMediaSample* pMS, LAVFrame *pFrame);

	HRESULT NegotiatePixelFormat(CMediaType &mt, int width, int height);
	BOOL IsInterlacedOutput();

	HRESULT CheckDirectMode();
	HRESULT DeDirectFrame(LAVFrame *pFrame, bool bDisableDirectMode = true);

	HRESULT Filter(LAVFrame *pFrame);
	HRESULT DeliverToRenderer(LAVFrame *pFrame);

	HRESULT PerformFlush();
	HRESULT ReleaseLastSequenceFrame();

private:

	friend class CVideoInputPin;
	friend class CVideoOutputPin;
	friend class CDecodeManager;

	CDecodeManager       m_Decoder;

	REFERENCE_TIME       m_rtPrevStart = 0;
	REFERENCE_TIME       m_rtPrevStop = 0;
	REFERENCE_TIME       m_rtAvgTimePerFrame = AV_NOPTS_VALUE;

	BOOL                 m_bForceInputAR = FALSE;
	BOOL                 m_bFlushing = FALSE;
	BOOL                 m_bForceFormatNegotiation = FALSE;

	HRESULT              m_hrDeliver = S_OK;

	CLAVPixFmtConverter  m_PixFmtConverter;
	std::wstring         m_strExtension;


	DWORD                m_dwDecodeFlags = 0;

	AVFilterGraph        *m_pFilterGraph = nullptr;
	AVFilterContext      *m_pFilterBufferSrc = nullptr;
	AVFilterContext      *m_pFilterBufferSink = nullptr;

	LAVPixelFormat       m_filterPixFmt = LAVPixFmt_None;
	int                  m_filterWidth = 0;
	int                  m_filterHeight = 0;
	LAVFrame             m_FilterPrevFrame;


	BOOL                 m_LAVPinInfoValid = FALSE;
	LAVPinInfo           m_LAVPinInfo;

	struct {
		AVMasteringDisplayMetadata Mastering;
	} m_SideData;

	LAVFrame             *m_pLastSequenceFrame = nullptr;


	BOOL                 m_bRuntimeConfig = FALSE;
	struct VideoSettings {
		DWORD StreamAR;
		DWORD NumThreads;
		BOOL bFormats[Codec_VideoNB];
		BOOL bPixFmts[LAVOutPixFmt_NB];
		DWORD RGBRange;
		DWORD DeintFieldOrder;
		LAVDeintMode DeintMode;
		DWORD SWDeintMode;
		DWORD SWDeintOutput;
		DWORD DitherMode;
	} m_settings;

	DWORD m_dwGPUDeviceIndex = DWORD_MAX;

};