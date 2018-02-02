#pragma once


#include <vector>
#include <string>
#include "PacketQueue.h"
#include "StreamParser.h"

#include "moreuuids.h"

#include "FMS30Demultiplexer.h"
#include "ILAVPinInfo.h"
#include "IBitRateInfo.h"
#include "IMediaSideData.h"

class CFMS30OutputPin
	:public CBaseOutputPin
	,public ILAVPinInfo
	,public IBitRateInfo
	,public IMediaSideData
	,public IMediaSeeking
	,protected CAMThread
{
public:
	CFMS30OutputPin(std::deque<CMediaType>& mts, LPCWSTR pName, CBaseFilter *pFilter, CCritSec *pLock, HRESULT *phr, CBaseDemuxer::StreamType pinType = CBaseDemuxer::unknown, const char* container = "");
	virtual ~CFMS30OutputPin();

	DECLARE_IUNKNOWN;
	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

	// CBaseOutputPin
	HRESULT DecideAllocator(IMemInputPin * pPin, IMemAllocator ** pAlloc);
	HRESULT DecideBufferSize(IMemAllocator* pAlloc, ALLOCATOR_PROPERTIES* pProperties);
	HRESULT CheckMediaType(const CMediaType* pmt);
	HRESULT GetMediaType(int iPosition, CMediaType* pmt);
	HRESULT Active();
	HRESULT Inactive();

	STDMETHODIMP Connect(IPin * pReceivePin, const AM_MEDIA_TYPE *pmt);
	HRESULT CompleteConnect(IPin *pReceivePin);

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

	// ILAVPinInfo
	STDMETHODIMP_(DWORD) GetStreamFlags();
	STDMETHODIMP_(int) GetPixelFormat();
	STDMETHODIMP_(int) GetVersion() { return 1; }
	STDMETHODIMP_(int) GetHasBFrames();

	// IBitRateInfo
	STDMETHODIMP_(DWORD) GetCurrentBitRate() { return m_BitRate.nCurrentBitRate; }
	STDMETHODIMP_(DWORD) GetAverageBitRate() { return m_BitRate.nAverageBitRate; }

	// IMediaSideData
	STDMETHODIMP SetSideData(GUID guidType, const BYTE *pData, size_t size) { return E_NOTIMPL; }
	STDMETHODIMP GetSideData(GUID guidType, const BYTE **pData, size_t *pSize);

	size_t QueueCount();
	HRESULT QueuePacket(Packet *pPacket);
	HRESULT QueueEndOfStream();
	bool IsDiscontinuous();
	DWORD GetQueueLowLimit() const { return m_dwQueueLow; }

	DWORD GetStreamId() { return m_streamId; };
	void SetStreamId(DWORD newStreamId) { m_streamId = newStreamId; };

	void SetNewMediaTypes(std::deque<CMediaType> pmts) { CAutoLock lock(&m_csMT); m_mts = pmts; SetQueueSizes(); }
	void SendMediaType(CMediaType *mt) { CAutoLock lock(&m_csMT); m_newMT = mt; }
	void SetStreamMediaType(CMediaType *mt) { CAutoLock lock(&m_csMT); m_StreamMT = *mt; }

	CMediaType& GetActiveMediaType() { return m_mt; }

	BOOL IsVideoPin() { return m_pinType == CBaseDemuxer::video; }
	BOOL IsAudioPin() { return m_pinType == CBaseDemuxer::audio; }
	BOOL IsSubtitlePin() { return m_pinType == CBaseDemuxer::subpic; }
	CBaseDemuxer::StreamType GetPinType() { return m_pinType; }

	HRESULT QueueFromParser(Packet *pPacket) { m_queue.Queue(pPacket); return S_OK; }
	HRESULT GetQueueSize(int& samples, int& size);

public:
	// Packet handling functions
	virtual HRESULT DeliverBeginFlush();
	virtual HRESULT DeliverEndFlush();

	virtual HRESULT DeliverNewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate);

	void SetQueueSizes();

	REFERENCE_TIME m_rtPrev = AV_NOPTS_VALUE;

protected:
	virtual HRESULT DeliverPacket(Packet *pPacket);

private:
	enum { CMD_EXIT };
	DWORD ThreadProc();

private:
	CCritSec m_csMT;
	std::deque<CMediaType> m_mts;
	CPacketQueue m_queue;
	CMediaType m_StreamMT;
	
	std::string m_containerFormat;

	// Flush control
	bool m_fFlushing = false;
	bool m_fFlushed = false;
	CAMEvent m_eEndFlush{ TRUE };

	HRESULT m_hrDeliver = S_OK;

	int m_nBuffers = 1;
	DWORD m_dwQueueLow = MIN_PACKETS_IN_QUEUE;
	DWORD m_dwQueueHigh = 350;
	DWORD m_dwQueueMaxMem = 256;

	DWORD m_streamId = 0;
	CMediaType *m_newMT = nullptr;

	CBaseDemuxer::StreamType m_pinType;

	CStreamParser m_Parser;
	BOOL m_bPacketAllocator = FALSE;

	// IBitRateInfo
	struct BitRateInfo {
		UINT64 nTotalBytesDelivered = 0;
		REFERENCE_TIME rtTotalTimeDelivered = 0;
		UINT64 nBytesSinceLastDeliverTime = 0;
		REFERENCE_TIME rtLastDeliverTime = Packet::INVALID_TIME;
		DWORD nCurrentBitRate = 0;
		DWORD nAverageBitRate = 0;
	} m_BitRate;
};

