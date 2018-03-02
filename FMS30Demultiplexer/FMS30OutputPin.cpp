#include "stdafx.h"
#include "FMS30OutputPin.h"

#include "PacketAllocator.h"

CFMS30OutputPin::CFMS30OutputPin(std::deque<CMediaType>& mts, LPCWSTR pName, CBaseFilter *pFilter, CCritSec *pLock, HRESULT *phr, CBaseDemuxer::StreamType pinType, const char* container)
	:CBaseOutputPin(NAME("FMS dshow output pin"),pFilter,pLock,phr,pName)
	,m_mts(mts)
	,m_containerFormat(container)
	,m_pinType(pinType)
	,m_Parser(this,container)
{
	SetQueueSizes();
}


CFMS30OutputPin::~CFMS30OutputPin()
{
	CAMThread::CallWorker(CMD_EXIT);
	CAMThread::Close();
	SAFE_DELETE(m_newMT);
}

STDMETHODIMP CFMS30OutputPin::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
	CheckPointer(ppv, E_POINTER);

	return
		QI(IMediaSeeking)
		QI(ILAVPinInfo)
		QI(IBitRateInfo)
		QI(IMediaSideData)
		__super::NonDelegatingQueryInterface(riid, ppv);
}

HRESULT CFMS30OutputPin::DecideAllocator(IMemInputPin * pPin, IMemAllocator ** ppAlloc)
{
	HRESULT hr = NOERROR;
	*ppAlloc = nullptr;

	// get downstream prop request
	// the derived class may modify this in DecideBufferSize, but
	// we assume that he will consistently modify it the same way,
	// so we only get it once
	ALLOCATOR_PROPERTIES prop;
	ZeroMemory(&prop, sizeof(prop));

	// whatever he returns, we assume prop is either all zeros
	// or he has filled it out.
	pPin->GetAllocatorRequirements(&prop);

	// if he doesn't care about alignment, then set it to 1
	if (prop.cbAlign == 0) {
		prop.cbAlign = 1;
	}

	*ppAlloc = new CPacketAllocator(NAME("CPacketAllocator"), nullptr, &hr);
	(*ppAlloc)->AddRef();
	if (SUCCEEDED(hr)) {
		DbgLog((LOG_TRACE, 10, L"Trying to use our CPacketAllocator"));
		m_bPacketAllocator = TRUE;
		hr = DecideBufferSize(*ppAlloc, &prop);
		if (SUCCEEDED(hr)) {
			DbgLog((LOG_TRACE, 10, L"-> DecideBufferSize Success"));
			hr = pPin->NotifyAllocator(*ppAlloc, FALSE);
			if (SUCCEEDED(hr)) {
				DbgLog((LOG_TRACE, 10, L"-> NotifyAllocator Success"));
				return NOERROR;
			}
		}
	}

	if (*ppAlloc) {
		(*ppAlloc)->Release();
		*ppAlloc = nullptr;
	}

	m_bPacketAllocator = FALSE;

	/* Try the allocator provided by the input pin */
	hr = pPin->GetAllocator(ppAlloc);
	if (SUCCEEDED(hr)) {

		hr = DecideBufferSize(*ppAlloc, &prop);
		if (SUCCEEDED(hr)) {
			hr = pPin->NotifyAllocator(*ppAlloc, FALSE);
			if (SUCCEEDED(hr)) {
				return NOERROR;
			}
		}
	}

	/* If the GetAllocator failed we may not have an interface */
	if (*ppAlloc) {
		(*ppAlloc)->Release();
		*ppAlloc = nullptr;
	}
	return hr;

}

HRESULT CFMS30OutputPin::DecideBufferSize(IMemAllocator* pAlloc, ALLOCATOR_PROPERTIES* pProperties)
{
	CheckPointer(pAlloc, E_POINTER);
	CheckPointer(pProperties, E_POINTER);

	HRESULT hr = S_OK;

	pProperties->cBuffers = max(pProperties->cBuffers, (m_bPacketAllocator ? 20 : m_nBuffers));
	pProperties->cbBuffer = max(max(m_mt.lSampleSize, 256000), (ULONG)pProperties->cbBuffer);

	// Vorbis requires at least 2 buffers
	if (m_mt.subtype == MEDIASUBTYPE_Vorbis && m_mt.formattype == FORMAT_VorbisFormat) {
		pProperties->cBuffers = max(pProperties->cBuffers, 2);
	}

	// Sanity checks
	ALLOCATOR_PROPERTIES Actual;
	if (FAILED(hr = pAlloc->SetProperties(pProperties, &Actual))) return hr;
	if (Actual.cbBuffer < pProperties->cbBuffer) return E_FAIL;
	ASSERT(Actual.cBuffers >= pProperties->cBuffers);

	return S_OK;
}

HRESULT CFMS30OutputPin::CheckMediaType(const CMediaType* pmt)
{
	for (auto it = m_mts.begin(); it != m_mts.end(); ++it)
	{
		if (*pmt == *it)
			return S_OK;
	}

	return E_INVALIDARG;
}

HRESULT CFMS30OutputPin::GetMediaType(int iPosition, CMediaType* pmt)
{
	DbgLog((LOG_TRACE, 10, L"CFMS30OutputPin::GetMediaType(): %s, position: %d", CBaseDemuxer::CStreamList::ToStringW(m_pinType), iPosition));
	CAutoLock cAutoLock(m_pLock);

	if (iPosition < 0) return E_INVALIDARG;
	if ((size_t)iPosition >= m_mts.size()) return VFW_S_NO_MORE_ITEMS;

	*pmt = m_mts[iPosition];

	return S_OK;
}

HRESULT CFMS30OutputPin::Active()
{
	DbgLog((LOG_TRACE, 30, L"CFMS30OutputPin::Active() - activated %s pin", CBaseDemuxer::CStreamList::ToStringW(m_pinType)));
	CAutoLock cAutoLock(m_pLock);

	if (m_Connected)
		Create();

	return __super::Active();
}

HRESULT CFMS30OutputPin::Inactive()
{
	DbgLog((LOG_TRACE, 30, L"CFMS30OutputPin::Inactive() - de-activated %s pin", CBaseDemuxer::CStreamList::ToStringW(m_pinType)));
	CAutoLock cAutoLock(m_pLock);

	CAMThread::CallWorker(CMD_EXIT);
	CAMThread::Close();

	// Clear queue when we're going inactive
	m_queue.Clear();

	return __super::Inactive();
}

STDMETHODIMP CFMS30OutputPin::Connect(IPin * pReceivePin, const AM_MEDIA_TYPE *pmt)
{
	HRESULT  hr;
	PIN_INFO PinInfo;

	if (SUCCEEDED(pReceivePin->QueryPinInfo(&PinInfo))) {
		PinInfo.pFilter->Release();
	}
	hr = __super::Connect(pReceivePin, pmt);
	return hr;
}

HRESULT CFMS30OutputPin::CompleteConnect(IPin *pReceivePin)
{
	m_StreamMT = m_mt;
	return __super::CompleteConnect(pReceivePin);
}

STDMETHODIMP CFMS30OutputPin::GetCapabilities(DWORD* pCapabilities)
{
	return (static_cast<CFMS30Splitter*>(m_pFilter))->GetCapabilities(pCapabilities);
}

STDMETHODIMP CFMS30OutputPin::CheckCapabilities(DWORD* pCapabilities)
{
	return (static_cast<CFMS30Splitter*>(m_pFilter))->CheckCapabilities(pCapabilities);
}

STDMETHODIMP CFMS30OutputPin::IsFormatSupported(const GUID* pFormat)
{
	return (static_cast<CFMS30Splitter*>(m_pFilter))->IsFormatSupported(pFormat);
}

STDMETHODIMP CFMS30OutputPin::QueryPreferredFormat(GUID* pFormat)
{
	return (static_cast<CFMS30Splitter*>(m_pFilter))->QueryPreferredFormat(pFormat);
}

STDMETHODIMP CFMS30OutputPin::GetTimeFormat(GUID* pFormat)
{
	return (static_cast<CFMS30Splitter*>(m_pFilter))->GetTimeFormat(pFormat);
}

STDMETHODIMP CFMS30OutputPin::IsUsingTimeFormat(const GUID* pFormat)
{
	return (static_cast<CFMS30Splitter*>(m_pFilter))->IsUsingTimeFormat(pFormat);
}

STDMETHODIMP CFMS30OutputPin::SetTimeFormat(const GUID* pFormat)
{
	return (static_cast<CFMS30Splitter*>(m_pFilter))->SetTimeFormat(pFormat);
}

STDMETHODIMP CFMS30OutputPin::GetDuration(LONGLONG* pDuration)
{
	return (static_cast<CFMS30Splitter*>(m_pFilter))->GetDuration(pDuration);
}

STDMETHODIMP CFMS30OutputPin::GetStopPosition(LONGLONG* pStop)
{
	return (static_cast<CFMS30Splitter*>(m_pFilter))->GetStopPosition(pStop);
}

STDMETHODIMP CFMS30OutputPin::GetCurrentPosition(LONGLONG* pCurrent)
{
	return (static_cast<CFMS30Splitter*>(m_pFilter))->GetCurrentPosition(pCurrent);
}

STDMETHODIMP CFMS30OutputPin::ConvertTimeFormat(LONGLONG* pTarget, const GUID* pTargetFormat, LONGLONG Source, const GUID* pSourceFormat)
{
	return (static_cast<CFMS30Splitter*>(m_pFilter))->ConvertTimeFormat(pTarget, pTargetFormat, Source, pSourceFormat);
}

STDMETHODIMP CFMS30OutputPin::SetPositions(LONGLONG* pCurrent, DWORD dwCurrentFlags, LONGLONG* pStop, DWORD dwStopFlags)
{
	return (static_cast<CFMS30Splitter*>(m_pFilter))->SetPositionsInternal(this, pCurrent, dwCurrentFlags, pStop, dwStopFlags);
}

STDMETHODIMP CFMS30OutputPin::GetPositions(LONGLONG* pCurrent, LONGLONG* pStop)
{
	return (static_cast<CFMS30Splitter*>(m_pFilter))->GetPositions(pCurrent, pStop);
}

STDMETHODIMP CFMS30OutputPin::GetAvailable(LONGLONG* pEarliest, LONGLONG* pLatest)
{
	return (static_cast<CFMS30Splitter*>(m_pFilter))->GetAvailable(pEarliest, pLatest);
}

STDMETHODIMP CFMS30OutputPin::SetRate(double dRate)
{
	return (static_cast<CFMS30Splitter*>(m_pFilter))->SetRate(dRate);
}

STDMETHODIMP CFMS30OutputPin::GetRate(double* pdRate)
{
	return (static_cast<CFMS30Splitter*>(m_pFilter))->GetRate(pdRate);
}

STDMETHODIMP CFMS30OutputPin::GetPreroll(LONGLONG* pllPreroll)
{
	return (static_cast<CFMS30Splitter*>(m_pFilter))->GetPreroll(pllPreroll);
}

STDMETHODIMP_(DWORD) CFMS30OutputPin::GetStreamFlags()
{
	return (static_cast<CFMS30Splitter*>(m_pFilter))->GetStreamFlags(m_streamId);
}

STDMETHODIMP_(int) CFMS30OutputPin::GetPixelFormat()
{
	return (static_cast<CFMS30Splitter*>(m_pFilter))->GetPixelFormat(m_streamId);
}

STDMETHODIMP_(int) CFMS30OutputPin::GetHasBFrames()
{
	return (static_cast<CFMS30Splitter*>(m_pFilter))->GetHasBFrames(m_streamId);
}

STDMETHODIMP CFMS30OutputPin::GetSideData(GUID guidType, const BYTE **pData, size_t *pSize)
{
	return (static_cast<CFMS30Splitter*>(m_pFilter))->GetSideData(m_streamId, guidType, pData, pSize);
}

size_t CFMS30OutputPin::QueueCount()
{
	return m_queue.Size();
}

HRESULT CFMS30OutputPin::QueuePacket(Packet *pPacket)
{
	if (!ThreadExists()) {
		SAFE_DELETE(pPacket);
		return S_FALSE;
	}

	CFMS30Splitter *pSplitter = static_cast<CFMS30Splitter*>(m_pFilter);

	// While everything is good AND no pin is drying AND the queue is full .. sleep
	// The queu has a "soft" limit of MAX_PACKETS_IN_QUEUE, and a hard limit of MAX_PACKETS_IN_QUEUE * 2
	// That means, even if one pin is drying, we'll never exceed MAX_PACKETS_IN_QUEUE * 2
	while (S_OK == m_hrDeliver
		&& (m_queue.DataSize() > m_dwQueueMaxMem
			|| m_queue.Size() > 2 * m_dwQueueHigh
			|| (m_queue.Size() > m_dwQueueHigh && !pSplitter->IsAnyPinDrying())))
		Sleep(10);

	if (S_OK != m_hrDeliver)
	{
		SAFE_DELETE(pPacket);
		return m_hrDeliver;
	}

	{
		CAutoLock lock(&m_csMT);
		if (m_newMT && pPacket) {
			DbgLog((LOG_TRACE, 10, L"::QueuePacket() - Found new Media Type"));
			pPacket->pmt = CreateMediaType(m_newMT);
			SetStreamMediaType(m_newMT);
			SAFE_DELETE(m_newMT);
		}
	}

	m_Parser.Parse(m_StreamMT.subtype, pPacket);

	return m_hrDeliver;
}

HRESULT CFMS30OutputPin::QueueEndOfStream()
{
	return QueuePacket(nullptr); // nullptr means EndOfStream
}

bool CFMS30OutputPin::IsDiscontinuous()
{
	return m_mt.majortype == MEDIATYPE_Text
		|| m_mt.majortype == MEDIATYPE_ScriptCommand
		|| m_mt.majortype == MEDIATYPE_Subtitle
		|| m_mt.subtype == MEDIASUBTYPE_DVD_SUBPICTURE
		|| m_mt.subtype == MEDIASUBTYPE_CVD_SUBPICTURE
		|| m_mt.subtype == MEDIASUBTYPE_SVCD_SUBPICTURE;
}

HRESULT CFMS30OutputPin::GetQueueSize(int& samples, int& size)
{
	CAutoLock lock(&m_queue);
	samples = (int)m_queue.Size();
	size = (int)m_queue.DataSize();
	return S_OK;
}

HRESULT CFMS30OutputPin::DeliverBeginFlush()
{
	DbgLog((LOG_TRACE, 20, L"::DeliverBeginFlush on %s Pin", CBaseDemuxer::CStreamList::ToStringW(m_pinType)));
	m_eEndFlush.Reset();
	m_fFlushed = false;
	m_fFlushing = true;
	m_hrDeliver = S_FALSE;
	m_queue.Clear();
	HRESULT hr = IsConnected() ? GetConnected()->BeginFlush() : S_OK;
	if (hr != S_OK) m_eEndFlush.Set();
	return hr;
}

HRESULT CFMS30OutputPin::DeliverEndFlush()
{
	DbgLog((LOG_TRACE, 20, L"::DeliverEndFlush on %s Pin", CBaseDemuxer::CStreamList::ToStringW(m_pinType)));
	HRESULT hr = IsConnected() ? GetConnected()->EndFlush() : S_OK;

	m_Parser.Flush();

	m_hrDeliver = S_OK;
	m_fFlushing = false;
	m_fFlushed = true;

	m_eEndFlush.Set();
	return hr;
}

HRESULT CFMS30OutputPin::DeliverNewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate)
{
	HRESULT hr = S_OK;
	DbgLog((LOG_TRACE, 20, L"::DeliverNewSegment on %s Pin (rtStart: %I64d; rtStop: %I64d)", CBaseDemuxer::CStreamList::ToStringW(m_pinType), tStart, tStop));
	m_rtPrev = AV_NOPTS_VALUE;
	if (m_fFlushing) return S_FALSE;
	if (!ThreadExists()) return S_FALSE;

	m_BitRate.rtLastDeliverTime = Packet::INVALID_TIME;
	hr = __super::DeliverNewSegment(tStart, tStop, dRate);
	if (hr != S_OK)
		return hr;

	return hr;
}

void CFMS30OutputPin::SetQueueSizes()
{
	int factor = 1;

	// Normalize common audio codecs to reach a base line of 20ms
	if (m_mts.begin()->subtype == MEDIASUBTYPE_DOLBY_TRUEHD) {
		DbgLog((LOG_TRACE, 10, L"Increasing Audio Queue size for TrueHD"));
		factor = 30;
	}
	else if (m_mts.begin()->subtype == MEDIASUBTYPE_HDMV_LPCM_AUDIO || m_mts.begin()->subtype == MEDIASUBTYPE_BD_LPCM_AUDIO || m_mts.begin()->subtype == MEDIASUBTYPE_PCM) {
		factor = 4;
	}
	else if (m_mts.begin()->subtype == MEDIASUBTYPE_DTS || m_mts.begin()->subtype == MEDIASUBTYPE_WAVE_DTS) {
		factor = 2;
	}

	if (m_mts.begin()->majortype == MEDIATYPE_Audio) {
		factor *= 4;
	}

	m_dwQueueLow = MIN_PACKETS_IN_QUEUE * factor;
	m_dwQueueHigh = (static_cast<CFMS30Splitter*>(m_pFilter))->GetMaxQueueSize() * factor;

	m_dwQueueMaxMem = (static_cast<CFMS30Splitter*>(m_pFilter))->GetMaxQueueMemSize() * 1024 * 1024;
	if (!m_dwQueueMaxMem) {
		m_dwQueueMaxMem = 256 * 1024 * 1024;
	}
}

HRESULT CFMS30OutputPin::DeliverPacket(Packet *pPacket)
{
	HRESULT hr = S_OK;
	IMediaSample *pSample = nullptr;

	long nBytes = (long)pPacket->GetDataSize();

	if (nBytes == 0)
		goto done;

	CHECK_HR(hr == GetDeliveryBuffer(&pSample, nullptr, nullptr, 0));

	if (pSample == nullptr)
		goto done;

	if (m_bPacketAllocator)
	{
		ILAVMediaSample *pLAVSample = nullptr;
		CHECK_HR(hr = pSample->QueryInterface(&pLAVSample));
		CHECK_HR(hr = pLAVSample->SetPacket(pPacket));
		SafeRelease(&pLAVSample);
	}
	else
	{
		// Resize buffer if it is too small
		// This can cause a playback hick-up, we should avoid this if possible by setting a big enough buffer size
		if (nBytes > pSample->GetSize()) {
			SafeRelease(&pSample);
			ALLOCATOR_PROPERTIES props, actual;
			CHECK_HR(hr = m_pAllocator->GetProperties(&props));
			// Give us 2 times the requested size, so we don't resize every time
			props.cbBuffer = nBytes * 2;
			if (props.cBuffers > 1) {
				CHECK_HR(hr = __super::DeliverBeginFlush());
				CHECK_HR(hr = __super::DeliverEndFlush());
			}
			CHECK_HR(hr = m_pAllocator->Decommit());
			CHECK_HR(hr = m_pAllocator->SetProperties(&props, &actual));
			CHECK_HR(hr = m_pAllocator->Commit());
			CHECK_HR(hr = GetDeliveryBuffer(&pSample, nullptr, nullptr, 0));
		}

		// Fill the sample
		BYTE* pData = nullptr;
		if (FAILED(hr = pSample->GetPointer(&pData)) || !pData) goto done;

		memcpy(pData, pPacket->GetData(), nBytes);
	}

	if (pPacket->pmt) {
		DbgLog((LOG_TRACE, 10, L"::DeliverPacket() - sending new media type to decoder"));
		pSample->SetMediaType(pPacket->pmt);
		pPacket->bDiscontinuity = true;

		CAutoLock cAutoLock(m_pLock);
		CMediaType pmt = *(pPacket->pmt);
		m_mts.clear();
		m_mts.push_back(pmt);
		pPacket->pmt = nullptr;

		SetMediaType(&pmt);
	}

	bool fTimeValid = pPacket->rtStart != Packet::INVALID_TIME;

	// IBitRateInfo
	m_BitRate.nBytesSinceLastDeliverTime += nBytes;

	if (fTimeValid)
	{
		if (m_BitRate.rtLastDeliverTime == Packet::INVALID_TIME) {
			m_BitRate.rtLastDeliverTime = pPacket->rtStart;
			m_BitRate.nBytesSinceLastDeliverTime = 0;
		}

		if (m_BitRate.rtLastDeliverTime + 10000000 < pPacket->rtStart)
		{
			REFERENCE_TIME rtDiff = pPacket->rtStart - m_BitRate.rtLastDeliverTime;

			double dSecs, dBits;

			dSecs = rtDiff / 10000000.0;
			dBits = 8.0 * m_BitRate.nBytesSinceLastDeliverTime;
			m_BitRate.nCurrentBitRate = (DWORD)(dBits / dSecs);

			m_BitRate.rtTotalTimeDelivered += rtDiff;
			m_BitRate.nTotalBytesDelivered += m_BitRate.nBytesSinceLastDeliverTime;

			dSecs = m_BitRate.rtTotalTimeDelivered / 10000000.0;
			dBits = 8.0 * m_BitRate.nTotalBytesDelivered;
			m_BitRate.nAverageBitRate = (DWORD)(dBits / dSecs);

			m_BitRate.rtLastDeliverTime = pPacket->rtStart;
			m_BitRate.nBytesSinceLastDeliverTime = 0;
		}
	}

	CHECK_HR(hr = pSample->SetActualDataLength(nBytes));
	CHECK_HR(hr = pSample->SetTime(fTimeValid ? &pPacket->rtStart : nullptr, fTimeValid ? &pPacket->rtStop : nullptr));
	CHECK_HR(hr = pSample->SetMediaTime(nullptr, nullptr));
	CHECK_HR(hr = pSample->SetDiscontinuity(pPacket->bDiscontinuity));
	CHECK_HR(hr = pSample->SetSyncPoint(pPacket->bSyncPoint));
	CHECK_HR(hr = pSample->SetPreroll(fTimeValid && pPacket->rtStart < 0));
	// Deliver
	CHECK_HR(hr = Deliver(pSample));

done:
	if (!m_bPacketAllocator || !pSample)
		SAFE_DELETE(pPacket);
	SafeRelease(&pSample);

	return hr;
}

DWORD CFMS30OutputPin::ThreadProc()
{
	std::string name = "CFMS30OutputPin " + std::string(CBaseDemuxer::CStreamList::ToString(m_pinType));
	SetThreadName(-1, name.c_str());

	m_hrDeliver = S_OK;
	m_fFlushing = m_fFlushed = false;
	m_eEndFlush.Set();
	bool bFailFlush = false;

	while (1)
	{
		Sleep(1);

		DWORD cmd;
		if (CheckRequest(&cmd))
		{
			cmd = GetRequest();
			Reply(S_OK);
			ASSERT(cmd == CMD_EXIT);
			return 0;
		}

		size_t cnt = 0;
		do 
		{
			Packet * pPacket = nullptr;

			// Get a packet from the queue (scoped for lock)
			{
				CAutoLock cAutoLock(&m_queue);
				if ((cnt = m_queue.Size()) > 0) {
					pPacket = m_queue.Get();
				}
			}

			// We need to check cnt instead of pPacket, since it can be nullptr for EndOfStream
			if (m_hrDeliver == S_OK && cnt > 0)
			{
				ASSERT(!m_fFlushing);
				m_fFlushed = false;

				// flushing can still start here, to release a blocked deliver call
				HRESULT hr = pPacket ? DeliverPacket(pPacket) : DeliverEndOfStream();

				// .. so, wait until flush finished
				m_eEndFlush.Wait();

				if (hr != S_OK && !m_fFlushed)
				{
					DbgLog((LOG_TRACE, 10, L"OutputPin::ThreadProc(): Delivery failed on %s pin, hr: %0#.8x", CBaseDemuxer::CStreamList::ToStringW(GetPinType()), hr));
					if (!bFailFlush && hr == S_FALSE) {
						DbgLog((LOG_TRACE, 10, L"OutputPin::ThreadProc(): Trying to revive it by flushing..."));
						GetConnected()->BeginFlush();
						GetConnected()->EndFlush();
						bFailFlush = true;
					}
					else {
						m_hrDeliver = hr;
					}
					break;
				}
			}
			else if (pPacket)
			{
				SAFE_DELETE(pPacket);
			}
		} while(cnt > 1 && m_hrDeliver == S_OK);
	}
}
