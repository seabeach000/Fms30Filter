// FMS30Demultiplexer.cpp : 定义 DLL 应用程序的导出函数。
//

#include "stdafx.h"
#include "FMS30Demultiplexer.h"
#include "FmsDemuxer.h"
#include "FMS30InputPin.h"
#include "FMS30OutputPin.h"

#include <Shlwapi.h>
#include <string>
#include <regex>
#include <algorithm>

#include "registry.h"



void CALLBACK CFMS30Splitter::StaticInit(BOOL bLoading, const CLSID *clsid)
{
	if (!bLoading) return;
	CFmsDemuxer::ffmpeg_init(false);
}

CFMS30Splitter::CFMS30Splitter(LPUNKNOWN pUnk, HRESULT* phr)
	:CBaseFilter(NAME("FMS30 dshow source filter"), pUnk, this, __uuidof(this), phr)
{
	CFmsDemuxer::ffmpeg_init(true);

	LoadSettings();

	m_pInput = new CFMS30InputPin(NAME("FMS Input Pin"), this, this, phr);

	m_ePlaybackInit.Set();

#ifdef DEBUG
	DbgSetModuleLevel(LOG_TRACE, DWORD_MAX);
	DbgSetModuleLevel(LOG_ERROR, DWORD_MAX);

#ifdef LAV_DEBUG_RELEASE
	DbgSetLogFileDesktop(LAVF_LOG_FILE);
#endif
#endif

}

CFMS30Splitter::~CFMS30Splitter()
{
	SAFE_DELETE(m_pInput);
	Close();

	// delete old pins
	for (CFMS30OutputPin *pPin : m_pRetiredPins) {
		delete pPin;
	}
	m_pRetiredPins.clear();

#if defined(DEBUG) && defined(LAV_DEBUG_RELEASE)
	DbgCloseLogFile();
#endif
}

STDMETHODIMP CFMS30Splitter::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
	CheckPointer(ppv, E_POINTER);

	*ppv = nullptr;

	if (m_pDemuxer && (riid == IID_IPropertyBag)) {
		return m_pDemuxer->QueryInterface(riid, ppv);
	}

	return
		QI(IMediaSeeking)
		//QI(IAMStreamSelect)
		//QI(ISpecifyPropertyPages)
		//QI(ISpecifyPropertyPages2)
		QI2(ILAVFSettings)
		QI2(ILAVFSettingsInternal)
		//QI(IObjectWithSite)
		//QI(IBufferInfo)
		__super::NonDelegatingQueryInterface(riid, ppv);
}

// CBaseSplitter
int CFMS30Splitter::GetPinCount()
{
	CAutoLock lock(&m_csPins);

	int count = (int)m_pPins.size();
	if (m_pInput)
		count++;

	return count;
}

CBasePin * CFMS30Splitter::GetPin(int n)
{
	CAutoLock lock(&m_csPins);

	if (n < 0 || n >= GetPinCount()) return nullptr;

	if (m_pInput) {
		if (n == 0)
			return m_pInput;
		else
			n--;
	}

	return (CBasePin *)m_pPins[n];
}

STDMETHODIMP CFMS30Splitter::GetClassID(CLSID* pClsID)
{
	CheckPointer(pClsID, E_POINTER);
	return __super::GetClassID(pClsID);
}

STDMETHODIMP CFMS30Splitter::Stop()
{
	CAutoLock cAutoLock(this);

	// Wait for playback to finish initializing
	m_ePlaybackInit.Wait();

	// Ask network operations to exit
	if (m_pDemuxer)
		m_pDemuxer->AbortOpening(1);

	DeliverBeginFlush();
	CAMThread::CallWorker(CMD_EXIT);
	CAMThread::Close();
	DeliverEndFlush();

	if (m_pDemuxer)
		m_pDemuxer->AbortOpening(0);

	HRESULT hr;
	if (FAILED(hr = __super::Stop())) {
		return hr;
	}

	return S_OK;

}

STDMETHODIMP CFMS30Splitter::Pause()
{
	CAutoLock cAutoLock(this);
	CheckPointer(m_pDemuxer, E_UNEXPECTED);

	FILTER_STATE fs = m_State;

	HRESULT hr;
	if (FAILED(hr = __super::Pause())) {
		return hr;
	}

	// The filter graph will set us to pause before running
	// So if we were stopped before, create the thread
	// Note that the splitter will always be running,
	// and even in pause mode fill up the buffers
	if (fs == State_Stopped) {
		// At this point, the graph is hopefully finished, tell the demuxer about all the cool things
		m_pDemuxer->SettingsChanged(static_cast<ILAVFSettingsInternal *>(this));

		// Create demuxing thread
		if (!ThreadExists())
			m_ePlaybackInit.Reset();
		Create();
	}

	return S_OK;
}

STDMETHODIMP CFMS30Splitter::Run(REFERENCE_TIME tStart)
{
	CAutoLock cAutoLock(this);

	HRESULT hr;
	if (FAILED(hr = __super::Run(tStart))) {
		return hr;
	}

	return S_OK;
}

STDMETHODIMP CFMS30Splitter::JoinFilterGraph(IFilterGraph * pGraph, LPCWSTR pName)
{
	CAutoLock cObjectLock(m_pLock);
	HRESULT hr = __super::JoinFilterGraph(pGraph, pName);

	return hr;
}

STDMETHODIMP CFMS30Splitter::Load(LPCOLESTR pszFileName, const AM_MEDIA_TYPE * pmt)
{
	CheckPointer(pszFileName, E_POINTER);
	if (m_State != State_Stopped) return E_UNEXPECTED;

	// Close, just in case we're being re-used
	Close();

	m_fileName = std::wstring(pszFileName);

	HRESULT hr = S_OK;
	SAFE_DELETE(m_pDemuxer);
	LPWSTR extension = PathFindExtensionW(pszFileName);

	DbgLog((LOG_TRACE, 10, L"::Load(): Opening file '%s' (extension: %s)", pszFileName, extension));

	m_pDemuxer = new CFmsDemuxer(this, this);

	if (FAILED(hr = m_pDemuxer->Open(pszFileName))) {
		SAFE_DELETE(m_pDemuxer);
		return hr;
	}
	m_pDemuxer->AddRef();

	return InitDemuxer();
}

STDMETHODIMP CFMS30Splitter::GetCurFile(LPOLESTR *ppszFileName, AM_MEDIA_TYPE *pmt)
{
	CheckPointer(ppszFileName, E_POINTER);

	size_t strlen = m_fileName.length() + 1;
	*ppszFileName = (LPOLESTR)CoTaskMemAlloc(sizeof(wchar_t) * strlen);

	if (!(*ppszFileName))
		return E_OUTOFMEMORY;

	wcsncpy_s(*ppszFileName, strlen, m_fileName.c_str(), _TRUNCATE);
	return S_OK;
}

// IMediaSeeking
STDMETHODIMP CFMS30Splitter::GetCapabilities(DWORD* pCapabilities)
{
	CheckPointer(pCapabilities, E_POINTER);

	*pCapabilities =
		AM_SEEKING_CanGetStopPos |
		AM_SEEKING_CanGetDuration |
		AM_SEEKING_CanSeekAbsolute |
		AM_SEEKING_CanSeekForwards |
		AM_SEEKING_CanSeekBackwards;

	return S_OK;
}

STDMETHODIMP CFMS30Splitter::CheckCapabilities(DWORD* pCapabilities)
{
	CheckPointer(pCapabilities, E_POINTER);
	// capabilities is empty, all is good
	if (*pCapabilities == 0) return S_OK;
	// read caps
	DWORD caps;
	GetCapabilities(&caps);

	// Store the caps that we wanted
	DWORD wantCaps = *pCapabilities;
	// Update pCapabilities with what we have
	*pCapabilities = caps & wantCaps;

	// if nothing matches, its a disaster!
	if (*pCapabilities == 0) return E_FAIL;
	// if all matches, its all good
	if (*pCapabilities == wantCaps) return S_OK;
	// otherwise, a partial match
	return S_FALSE;
}

STDMETHODIMP CFMS30Splitter::IsFormatSupported(const GUID* pFormat)
{
	return !pFormat ? E_POINTER : *pFormat == TIME_FORMAT_MEDIA_TIME ? S_OK : S_FALSE;
}

STDMETHODIMP CFMS30Splitter::QueryPreferredFormat(GUID* pFormat)
{
	return GetTimeFormat(pFormat);
}

STDMETHODIMP CFMS30Splitter::GetTimeFormat(GUID* pFormat)
{
	return pFormat ? *pFormat = TIME_FORMAT_MEDIA_TIME, S_OK : E_POINTER;
}

STDMETHODIMP CFMS30Splitter::IsUsingTimeFormat(const GUID* pFormat)
{
	return IsFormatSupported(pFormat);
}

STDMETHODIMP CFMS30Splitter::SetTimeFormat(const GUID* pFormat)
{
	return S_OK == IsFormatSupported(pFormat) ? S_OK : E_INVALIDARG;
}

STDMETHODIMP CFMS30Splitter::GetDuration(LONGLONG* pDuration)
{
	REFERENCE_TIME rtDuration = -1;
	CheckPointer(pDuration, E_POINTER);
	CheckPointer(m_pDemuxer, E_UNEXPECTED);

	if (m_pInput) {
		if (FAILED(m_pInput->GetStreamDuration(&rtDuration)))
			rtDuration = -1;
	}

	if (rtDuration < 0)
		rtDuration = m_pDemuxer->GetDuration();

	if (rtDuration < 0)
		return E_FAIL;

	*pDuration = rtDuration;
	return S_OK;
}

STDMETHODIMP CFMS30Splitter::GetStopPosition(LONGLONG* pStop)
{
	return GetDuration(pStop);
}

STDMETHODIMP CFMS30Splitter::GetCurrentPosition(LONGLONG* pCurrent)
{
	return E_NOTIMPL;
}

STDMETHODIMP CFMS30Splitter::ConvertTimeFormat(LONGLONG* pTarget, const GUID* pTargetFormat, LONGLONG Source, const GUID* pSourceFormat)
{
	return E_NOTIMPL;
}

STDMETHODIMP CFMS30Splitter::SetPositions(LONGLONG* pCurrent, DWORD dwCurrentFlags, LONGLONG* pStop, DWORD dwStopFlags)
{
	return SetPositionsInternal(this, pCurrent, dwCurrentFlags, pStop, dwStopFlags);
}

STDMETHODIMP CFMS30Splitter::GetPositions(LONGLONG* pCurrent, LONGLONG* pStop)
{
	if (pCurrent) *pCurrent = m_rtCurrent;
	if (pStop) *pStop = m_rtStop;
	return S_OK;
}

STDMETHODIMP CFMS30Splitter::GetAvailable(LONGLONG* pEarliest, LONGLONG* pLatest)
{
	if (pEarliest) *pEarliest = 0;
	return GetDuration(pLatest);
}

STDMETHODIMP CFMS30Splitter::SetRate(double dRate)
{
	return dRate > 0 ? m_dRate = dRate, S_OK : E_INVALIDARG;
}

STDMETHODIMP CFMS30Splitter::GetRate(double* pdRate)
{
	return pdRate ? *pdRate = m_dRate, S_OK : E_POINTER;
}

STDMETHODIMP CFMS30Splitter::GetPreroll(LONGLONG* pllPreroll)
{
	return pllPreroll ? *pllPreroll = 0, S_OK : E_POINTER;
}

STDMETHODIMP CFMS30Splitter::QueryProgress(LONGLONG *pllTotal, LONGLONG *pllCurrent)
{
	return E_NOTIMPL;
}

STDMETHODIMP CFMS30Splitter::AbortOperation()
{
	if (m_pDemuxer)
		return m_pDemuxer->AbortOpening();
	else
		return E_UNEXPECTED;
}

STDMETHODIMP CFMS30Splitter::SetMaxQueueMemSize(DWORD dwMaxSize)
{
	m_settings.QueueMaxMemSize = dwMaxSize;
	for (auto it = m_pPins.begin(); it != m_pPins.end(); it++) {
		(*it)->SetQueueSizes();
	}
	//
	return S_OK;
}

STDMETHODIMP_(DWORD) CFMS30Splitter::GetMaxQueueMemSize()
{
	return m_settings.QueueMaxMemSize;
}

STDMETHODIMP CFMS30Splitter::SetNetworkStreamAnalysisDuration(DWORD dwDuration)
{
	m_settings.NetworkAnalysisDuration = dwDuration;
	return S_OK;
}

STDMETHODIMP_(DWORD) CFMS30Splitter::GetNetworkStreamAnalysisDuration()
{
	return m_settings.NetworkAnalysisDuration;
}

STDMETHODIMP CFMS30Splitter::SetMaxQueueSize(DWORD dwMaxSize)
{
	m_settings.QueueMaxPackets = dwMaxSize;
	for (auto it = m_pPins.begin(); it != m_pPins.end(); it++) {
		(*it)->SetQueueSizes();
	}
		return S_OK;
}

STDMETHODIMP_(CMediaType *) CFMS30Splitter::GetOutputMediatype(int stream)
{
	CFMS30OutputPin* pPin = GetOutputPin(stream, FALSE);
	if (!pPin || !pPin->IsConnected())
		return nullptr;

	CMediaType *pmt = new CMediaType(pPin->GetActiveMediaType());
	return pmt;
}

bool CFMS30Splitter::IsAnyPinDrying()
{
	// MPC changes thread priority here
	// TODO: Investigate if that is needed
	for (CFMS30OutputPin *pPin : m_pActivePins) {
		if (pPin->IsConnected() && !pPin->IsDiscontinuous() && pPin->QueueCount() < pPin->GetQueueLowLimit()) {
			return true;
		}
	}
	return false;
}

STDMETHODIMP_(DWORD) CFMS30Splitter::GetMaxQueueSize()
{
	return m_settings.QueueMaxPackets;
}

//Worker Thread
DWORD CFMS30Splitter::ThreadProc()
{
	std::vector<CFMS30OutputPin *>::iterator pinIter;

	CheckPointer(m_pDemuxer, 0);

	SetThreadName(-1, "CFMS30Splitter Demux");

	m_pDemuxer->Start();

	m_fFlushing = false;
	m_eEndFlush.Set();
	for (DWORD cmd = (DWORD)-1; ;cmd = GetRequest())
	{
		if (cmd == CMD_EXIT)
		{
			Reply(S_OK);
			m_ePlaybackInit.Set();
			return 0;
		}

		m_ePlaybackInit.Reset();

		m_rtStart = m_rtNewStart;
		m_rtStop = m_rtNewStop;

		if (m_bPlaybackStarted || m_rtStart != 0 || cmd == CMD_SEEK) {
			HRESULT hr = S_FALSE;
			if (m_pInput) {
				hr = m_pInput->SeekStream(m_rtStart);
				if (SUCCEEDED(hr))
					m_pDemuxer->Reset();
			}
			if (hr != S_OK)
				DemuxSeek(m_rtStart);
		}

		if (cmd != (DWORD)-1)
			Reply(S_OK);

		// Wait for the end of any flush
		m_eEndFlush.Wait();

		m_pActivePins.clear();

		for (pinIter = m_pPins.begin(); pinIter != m_pPins.end() && !m_fFlushing; ++pinIter) {
			if ((*pinIter)->IsConnected()) {
				(*pinIter)->DeliverNewSegment(m_rtStart, m_rtStop, m_dRate);
				m_pActivePins.push_back(*pinIter);
			}
		}
		m_rtOffset = AV_NOPTS_VALUE;

		m_bDiscontinuitySent.clear();

		m_bPlaybackStarted = TRUE;
		m_ePlaybackInit.Set();

		HRESULT hr = S_OK;
		while (SUCCEEDED(hr) && !CheckRequest(&cmd)) {
			hr = DemuxNextPacket();
		}

		// If we didnt exit by request, deliver end-of-stream
		if (!CheckRequest(&cmd)) {
			for (pinIter = m_pActivePins.begin(); pinIter != m_pActivePins.end(); ++pinIter) {
				(*pinIter)->QueueEndOfStream();
			}
		}
	}
	ASSERT(0); // we should only exit via CMD_EXIT

	return 0;
}

// Seek to the specified time stamp
HRESULT CFMS30Splitter::DemuxSeek(REFERENCE_TIME rtStart)
{
	if (rtStart < 0) { rtStart = 0; }

	return m_pDemuxer->Seek(rtStart);
}

// Demux the next packet and deliver it to the output pins
// Based on DVDDemuxFFMPEG
HRESULT CFMS30Splitter::DemuxNextPacket()
{
	Packet *pPacket;
	HRESULT hr = S_OK;
	hr = m_pDemuxer->GetNextPacket(&pPacket);
	// Only S_OK indicates we have a proper packet
	// S_FALSE is a "soft error", don't deliver the packet
	if (hr != S_OK) {
		return hr;
	}
	return DeliverPacket(pPacket);
}

HRESULT CFMS30Splitter::DeliverPacket(Packet *pPacket)
{
	HRESULT hr = S_FALSE;

	if (pPacket->dwFlags & LAV_PACKET_FORCED_SUBTITLE)
		pPacket->StreamId = FORCED_SUBTITLE_PID;

	CFMS30OutputPin* pPin = GetOutputPin(pPacket->StreamId, TRUE);
	if (!pPin || !pPin->IsConnected()) {
		delete pPacket;
		return S_FALSE;
	}

	if (pPacket->rtStart != Packet::INVALID_TIME) {
		m_rtCurrent = pPacket->rtStop;

		if (m_bStopValid && m_rtStop && pPacket->rtStart > m_rtStop) {
			DbgLog((LOG_TRACE, 10, L"::DeliverPacket(): Reached the designated stop time of %I64d at %I64d", m_rtStop, pPacket->rtStart));
			delete pPacket;
			return E_FAIL;
		}

		pPacket->rtStart -= m_rtStart;
		pPacket->rtStop -= m_rtStart;

		ASSERT(pPacket->rtStart <= pPacket->rtStop);

		// Filter PTS values
		//wxg20180201 暂不考虑时间戳跳变的情况

		pPacket->rtStart = (REFERENCE_TIME)(pPacket->rtStart / m_dRate);
		pPacket->rtStop = (REFERENCE_TIME)(pPacket->rtStop / m_dRate);
	}

	if (m_bDiscontinuitySent.find(pPacket->StreamId) == m_bDiscontinuitySent.end()) {
		pPacket->bDiscontinuity = TRUE;
	}

	BOOL bDiscontinuity = pPacket->bDiscontinuity;
	DWORD streamId = pPacket->StreamId;

	hr = pPin->QueuePacket(pPacket);

	if (hr != S_OK) {
		// Find a iterator pointing to the pin
		std::vector<CFMS30OutputPin *>::iterator it = std::find(m_pActivePins.begin(), m_pActivePins.end(), pPin);
		// Remove it from the vector
		m_pActivePins.erase(it);

		// Fail if no active pins remain, otherwise resume demuxing
		return m_pActivePins.empty() ? E_FAIL : S_OK;
	}

	if (bDiscontinuity) {
		m_bDiscontinuitySent.insert(streamId);
	}

	return hr;
}

void CFMS30Splitter::DeliverBeginFlush()
{
	m_fFlushing = true;

	// flush all pins
	for (CFMS30OutputPin *pPin : m_pPins) {
		pPin->DeliverBeginFlush();
	}
}

void CFMS30Splitter::DeliverEndFlush()
{
	// flush all pins
	for (CFMS30OutputPin *pPin : m_pPins) {
		pPin->DeliverEndFlush();
	}

	m_fFlushing = false;
	m_eEndFlush.Set();
}

STDMETHODIMP CFMS30Splitter::Close()
{
	CAutoLock cAutoLock(this);

	AbortOperation();
	CAMThread::CallWorker(CMD_EXIT);
	CAMThread::Close();

	m_State = State_Stopped;
	DeleteOutputs();

	SafeRelease(&m_pDemuxer);

	return S_OK;
}

STDMETHODIMP CFMS30Splitter::DeleteOutputs()
{
	CAutoLock lock(this);
	if (m_State != State_Stopped) return VFW_E_NOT_STOPPED;

	CAutoLock pinLock(&m_csPins);
	// Release pins
	for (CFMS30OutputPin *pPin : m_pPins) {
		if (IPin* pPinTo = pPin->GetConnected()) pPinTo->Disconnect();
		pPin->Disconnect();
		m_pRetiredPins.push_back(pPin);
	}
	m_pPins.clear();

	return S_OK;
}

STDMETHODIMP CFMS30Splitter::InitDemuxer()
{
	HRESULT hr = S_OK;

	m_rtStart = m_rtNewStart = m_rtCurrent = 0;
	m_rtStop = m_rtNewStop = m_pDemuxer->GetDuration();
	m_bPlaybackStarted = FALSE;

	const CBaseDemuxer::stream *videoStream = m_pDemuxer->SelectVideoStream();
	if (videoStream) {
		CFMS30OutputPin* pPin = new CFMS30OutputPin(videoStream->streamInfo->mtypes, CBaseDemuxer::CStreamList::ToStringW(CBaseDemuxer::video), this, this, &hr, CBaseDemuxer::video, m_pDemuxer->GetContainerFormat());
		if (SUCCEEDED(hr)) {
			pPin->SetStreamId(videoStream->pid);
			m_pPins.push_back(pPin);
			m_pDemuxer->SetActiveStream(CBaseDemuxer::video, videoStream->pid);
		}
		else {
			delete pPin;
		}
	}

	std::list<std::string> audioLangs;
	std::list<CBaseDemuxer::stream *> lstA = m_pDemuxer->SelectAllAudioStream(audioLangs);
	for (auto& ia : lstA)
	{
		const CBaseDemuxer::stream *audioStream = ia;
			if (audioStream) {
				CFMS30OutputPin* pPin = new CFMS30OutputPin(audioStream->streamInfo->mtypes, CBaseDemuxer::CStreamList::ToStringW(CBaseDemuxer::audio), this, this, &hr, CBaseDemuxer::audio, m_pDemuxer->GetContainerFormat());
				if (SUCCEEDED(hr)) {
					pPin->SetStreamId(audioStream->pid);
					m_pPins.push_back(pPin);
					m_pDemuxer->SetActiveStream(CBaseDemuxer::audio, audioStream->pid);
				}
				else {
					delete pPin;
				}
			}

	}
	

	if (SUCCEEDED(hr)) {
		// If there are no pins, what good are we?
		return !m_pPins.empty() ? S_OK : E_FAIL;
	}
	else {
		return hr;
	}
}

STDMETHODIMP CFMS30Splitter::SetPositionsInternal(void *caller, LONGLONG* pCurrent, DWORD dwCurrentFlags, LONGLONG* pStop, DWORD dwStopFlags)
{
	DbgLog((LOG_TRACE, 20, "::SetPositions() - seek request; caller: %p, current: %I64d; start: %I64d; flags: 0x%x, stop: %I64d; flags: 0x%x", caller, m_rtCurrent, pCurrent ? *pCurrent : -1, dwCurrentFlags, pStop ? *pStop : -1, dwStopFlags));
	CAutoLock cAutoLock(this);

	if (!pCurrent && !pStop
		|| (dwCurrentFlags&AM_SEEKING_PositioningBitsMask) == AM_SEEKING_NoPositioning
		&& (dwStopFlags&AM_SEEKING_PositioningBitsMask) == AM_SEEKING_NoPositioning) {
		return S_OK;
	}

	REFERENCE_TIME
		rtCurrent = m_rtCurrent,
		rtStop = m_rtStop;

	if (pCurrent) {
		switch (dwCurrentFlags&AM_SEEKING_PositioningBitsMask)
		{
		case AM_SEEKING_NoPositioning: break;
		case AM_SEEKING_AbsolutePositioning: rtCurrent = *pCurrent; break;
		case AM_SEEKING_RelativePositioning: rtCurrent = rtCurrent + *pCurrent; break;
		case AM_SEEKING_IncrementalPositioning: rtCurrent = rtCurrent + *pCurrent; break;
		}
	}

	if (pStop) {
		switch (dwStopFlags&AM_SEEKING_PositioningBitsMask)
		{
		case AM_SEEKING_NoPositioning: break;
		case AM_SEEKING_AbsolutePositioning: rtStop = *pStop; m_bStopValid = TRUE; break;
		case AM_SEEKING_RelativePositioning: rtStop += *pStop; m_bStopValid = TRUE; break;
		case AM_SEEKING_IncrementalPositioning: rtStop = rtCurrent + *pStop; m_bStopValid = TRUE; break;
		}
	}

	if (m_rtCurrent == rtCurrent && m_rtStop == rtStop) {
		return S_OK;
	}

	if (m_rtLastStart == rtCurrent && m_rtLastStop == rtStop && m_LastSeekers.find(caller) == m_LastSeekers.end()) {
		m_LastSeekers.insert(caller);
		return S_OK;
	}

	m_rtLastStart = rtCurrent;
	m_rtLastStop = rtStop;
	m_LastSeekers.clear();
	m_LastSeekers.insert(caller);

	m_rtNewStart = m_rtCurrent = rtCurrent;
	m_rtNewStop = rtStop;

	DbgLog((LOG_TRACE, 20, " -> Performing seek to %I64d", m_rtNewStart));
	if (ThreadExists())
	{
		DeliverBeginFlush();
		CallWorker(CMD_SEEK);
		DeliverEndFlush();
	}
	DbgLog((LOG_TRACE, 20, " -> Seek finished", m_rtNewStart));

	return S_OK;
}

CFMS30OutputPin * CFMS30Splitter::GetOutputPin(DWORD streamId, BOOL bActiveOnly /*= FALSE*/)
{
	CAutoLock lock(&m_csPins);

	auto &vec = bActiveOnly ? m_pActivePins : m_pPins;
	for (CFMS30OutputPin *pPin : vec) {
		if (pPin->GetStreamId() == streamId) {
			return pPin;
		}
	}
	return nullptr;
}

STDMETHODIMP CFMS30Splitter::CompleteInputConnection()
{
	HRESULT hr = S_OK;
	BOOL bFileInput = FALSE;

	SAFE_DELETE(m_pDemuxer);

	AVIOContext *pContext = nullptr;

	if (FAILED(hr = m_pInput->GetAVIOContext(&pContext))) {
		return hr;
	}

	LPOLESTR pszFileName = nullptr;

	PIN_INFO info;
	hr = m_pInput->GetConnected()->QueryPinInfo(&info);
	if (SUCCEEDED(hr) && info.pFilter) {
		IFileSourceFilter *pSource = nullptr;
		if (SUCCEEDED(info.pFilter->QueryInterface(&pSource)) && pSource) {
			pSource->GetCurFile(&pszFileName, nullptr);
			SafeRelease(&pSource);
		}
		CLSID inputCLSID;
		if (SUCCEEDED(info.pFilter->GetClassID(&inputCLSID))) {
			bFileInput = (inputCLSID == CLSID_AsyncReader);
		}
		SafeRelease(&info.pFilter);
	}

	const char *format = nullptr;
	if (m_pInput->CurrentMediaType().subtype == MEDIASUBTYPE_MPEG2_TRANSPORT) {
		format = "mpegts";
	}

	CFmsDemuxer *pDemux = new CFmsDemuxer(this, this);
	if (FAILED(hr = pDemux->OpenInputStream(pContext, pszFileName, format, FALSE, bFileInput)))
	{
		SAFE_DELETE(pDemux);
		return hr;
	}

	m_pDemuxer = pDemux;
	m_pDemuxer->AddRef();

	SAFE_CO_FREE(pszFileName);

	return InitDemuxer();
}

STDMETHODIMP CFMS30Splitter::BreakInputConnection()
{
	return Close();
}

STDMETHODIMP CFMS30Splitter::LoadDefaults()
{
	m_settings.QueueMaxMemSize = 256;
	m_settings.QueueMaxPackets = 350;
	m_settings.NetworkAnalysisDuration = 1000;

	return S_OK;
}

STDMETHODIMP CFMS30Splitter::ReadSettings(HKEY rootKey)
{
	return S_OK;
}

STDMETHODIMP CFMS30Splitter::LoadSettings()
{
	LoadDefaults();

	return S_OK;
}

STDMETHODIMP CFMS30Splitter::SaveSettings()
{
	return S_OK;
}

CFMS30SplitterSource::CFMS30SplitterSource(LPUNKNOWN pUnk, HRESULT* phr)
	:CFMS30Splitter(pUnk,phr)
{
	m_clsid = __uuidof(CFMS30SplitterSource);
	SAFE_DELETE(m_pInput);
}

CFMS30SplitterSource::~CFMS30SplitterSource()
{

}

STDMETHODIMP CFMS30SplitterSource::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
	CheckPointer(ppv, E_POINTER);

	*ppv = nullptr;

	return
		QI(IFileSourceFilter)
		QI(IAMOpenProgress)
		__super::NonDelegatingQueryInterface(riid, ppv);
}
