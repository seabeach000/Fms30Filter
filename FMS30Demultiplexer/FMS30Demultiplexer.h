// FMS30Demultiplexer.cpp : 定义 DLL 应用程序的导出函数。
//

#pragma once

#include <string>
#include <list>
#include <set>
#include <vector>
#include <map>

#include "stdafx.h"

#include "BaseDemuxer.h"

#include "LAVSplitterSettingsInternal.h"


class CFMS30OutputPin;
class CFMS30InputPin;

#ifdef	_MSC_VER
#pragma warning(disable: 4355)
#endif

class __declspec(uuid("E5703B48-E6C8-47C7-A418-683B31B8EE45")) CFMS30Splitter
	:public CBaseFilter
	,public CCritSec
	,protected CAMThread
	,public IFileSourceFilter
	,public IMediaSeeking
	,public IAMOpenProgress
	,public ILAVFSettingsInternal
{
public:
	CFMS30Splitter(LPUNKNOWN pUnk, HRESULT* phr);
	virtual ~CFMS30Splitter();

	static void CALLBACK StaticInit(BOOL bLoading, const CLSID *clsid);

	// IUnknown
	DECLARE_IUNKNOWN;
	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

	// CBaseFilter methods
	int GetPinCount();
	CBasePin *GetPin(int n);
	STDMETHODIMP GetClassID(CLSID* pClsID);

	STDMETHODIMP Stop();
	STDMETHODIMP Pause();
	STDMETHODIMP Run(REFERENCE_TIME tStart);

	STDMETHODIMP JoinFilterGraph(IFilterGraph * pGraph, LPCWSTR pName);

	// IFileSourceFilter
	STDMETHODIMP Load(LPCOLESTR pszFileName, const AM_MEDIA_TYPE * pmt);
	STDMETHODIMP GetCurFile(LPOLESTR *ppszFileName, AM_MEDIA_TYPE *pmt);

	// IMediaSeeking
	STDMETHODIMP GetCapabilities(DWORD* pCapabilities);
	STDMETHODIMP CheckCapabilities(DWORD* pCapabilities);
	STDMETHODIMP IsFormatSupported(const GUID* pFormat);
	STDMETHODIMP QueryPreferredFormat(GUID* pFormat);
	STDMETHODIMP GetTimeFormat(GUID* pFormat);
	STDMETHODIMP IsUsingTimeFormat(const GUID* pFormat);
	STDMETHODIMP SetTimeFormat(const GUID* pFormat);
	STDMETHODIMP GetDuration(LONGLONG* pDuration);
	STDMETHODIMP GetStopPosition(LONGLONG* pStop);
	STDMETHODIMP GetCurrentPosition(LONGLONG* pCurrent);
	STDMETHODIMP ConvertTimeFormat(LONGLONG* pTarget, const GUID* pTargetFormat, LONGLONG Source, const GUID* pSourceFormat);
	STDMETHODIMP SetPositions(LONGLONG* pCurrent, DWORD dwCurrentFlags, LONGLONG* pStop, DWORD dwStopFlags);
	STDMETHODIMP GetPositions(LONGLONG* pCurrent, LONGLONG* pStop);
	STDMETHODIMP GetAvailable(LONGLONG* pEarliest, LONGLONG* pLatest);
	STDMETHODIMP SetRate(double dRate);
	STDMETHODIMP GetRate(double* pdRate);
	STDMETHODIMP GetPreroll(LONGLONG* pllPreroll);

	// IAMOpenProgress
	STDMETHODIMP QueryProgress(LONGLONG *pllTotal, LONGLONG *pllCurrent);
	STDMETHODIMP AbortOperation();

	// ILAVFSettings
	STDMETHODIMP SetMaxQueueMemSize(DWORD dwMaxSize);
	STDMETHODIMP_(DWORD) GetMaxQueueMemSize();
	STDMETHODIMP SetNetworkStreamAnalysisDuration(DWORD dwDuration);
	STDMETHODIMP_(DWORD) GetNetworkStreamAnalysisDuration();
	STDMETHODIMP SetMaxQueueSize(DWORD dwMaxSize);
	STDMETHODIMP_(DWORD) GetMaxQueueSize();

	// ILAVSplitterSettingsInternal
	STDMETHODIMP_(LPCSTR) GetInputFormat() { if (m_pDemuxer) return m_pDemuxer->GetContainerFormat(); return nullptr; }
	STDMETHODIMP_(CMediaType *) GetOutputMediatype(int stream);
	STDMETHODIMP_(IFilterGraph *) GetFilterGraph() { if (m_pGraph) { m_pGraph->AddRef(); return m_pGraph; } return nullptr; }



	STDMETHODIMP_(DWORD) GetStreamFlags(DWORD dwStream) { if (m_pDemuxer) return m_pDemuxer->GetStreamFlags(dwStream); return 0; }
	STDMETHODIMP_(int) GetPixelFormat(DWORD dwStream) { if (m_pDemuxer) return m_pDemuxer->GetPixelFormat(dwStream); return AV_PIX_FMT_NONE; }
	STDMETHODIMP_(int) GetHasBFrames(DWORD dwStream) { if (m_pDemuxer) return m_pDemuxer->GetHasBFrames(dwStream); return -1; }
	STDMETHODIMP GetSideData(DWORD dwStream, GUID guidType, const BYTE **pData, size_t *pSize) { if (m_pDemuxer) return m_pDemuxer->GetSideData(dwStream, guidType, pData, pSize); else return E_FAIL; }


	bool IsAnyPinDrying();

protected:
	// CAMThread
	enum { CMD_EXIT, CMD_SEEK };
	DWORD ThreadProc();

	HRESULT DemuxSeek(REFERENCE_TIME rtStart);
	HRESULT DemuxNextPacket();
	HRESULT DeliverPacket(Packet *pPacket);

	void DeliverBeginFlush();
	void DeliverEndFlush();

	STDMETHODIMP Close();
	STDMETHODIMP DeleteOutputs();

	STDMETHODIMP InitDemuxer();

	friend class CFMS30OutputPin;
	STDMETHODIMP SetPositionsInternal(void *caller, LONGLONG* pCurrent, DWORD dwCurrentFlags, LONGLONG* pStop, DWORD dwStopFlags);

public:
	CFMS30OutputPin *GetOutputPin(DWORD streamId, BOOL bActiveOnly = FALSE);

	STDMETHODIMP CompleteInputConnection();
	STDMETHODIMP BreakInputConnection();

protected:
	STDMETHODIMP LoadDefaults();
	STDMETHODIMP ReadSettings(HKEY rootKey);
	STDMETHODIMP LoadSettings();
	STDMETHODIMP SaveSettings();

protected:
	CFMS30InputPin *m_pInput;

private:
	CCritSec m_csPins;
	std::vector<CFMS30OutputPin *> m_pPins;
	std::vector<CFMS30OutputPin *> m_pActivePins;
	std::vector<CFMS30OutputPin *> m_pRetiredPins;
	std::set<DWORD> m_bDiscontinuitySent;

	std::wstring m_fileName;
	std::wstring m_processName;

	CBaseDemuxer *m_pDemuxer = nullptr;

	BOOL m_bPlaybackStarted = FALSE;
	BOOL m_bFakeASFReader = FALSE;

	// Times
	REFERENCE_TIME m_rtStart = 0;
	REFERENCE_TIME m_rtStop = 0;
	REFERENCE_TIME m_rtCurrent = 0;
	REFERENCE_TIME m_rtNewStart = 0;
	REFERENCE_TIME m_rtNewStop = 0;
	REFERENCE_TIME m_rtOffset = AV_NOPTS_VALUE;
	double m_dRate = 1.0;
	BOOL m_bStopValid = FALSE;

	// Seeking
	REFERENCE_TIME m_rtLastStart = _I64_MIN;
	REFERENCE_TIME m_rtLastStop = _I64_MIN;
	std::set<void *> m_LastSeekers;


	// Settings
	struct Settings {
		DWORD QueueMaxPackets;
		DWORD QueueMaxMemSize;
		DWORD NetworkAnalysisDuration;
	} m_settings;

	CAMEvent m_ePlaybackInit{ TRUE };

	// flushing
	bool m_fFlushing = FALSE;
	CAMEvent m_eEndFlush;
	
};

class __declspec(uuid("A77A2527-B694-4177-8BB7-C4638F19717D")) CFMS30SplitterSource : public CFMS30Splitter
{
public:
	// construct only via class factory
	CFMS30SplitterSource(LPUNKNOWN pUnk, HRESULT* phr);
	virtual ~CFMS30SplitterSource();

	// IUnknown
	DECLARE_IUNKNOWN;
	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);
};