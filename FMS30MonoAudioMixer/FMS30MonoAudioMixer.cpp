// FMS30MonoAudioMixer.cpp : 定义 DLL 应用程序的导出函数。
//

#include "stdafx.h"
#include "FMS30MonoAudioMixer.h"

#include "DeCSS/DeCSSInputPin.h"
#include "Media.h"
#include <MMReg.h>
#include "IMediaSideData.h"
#include "IMediaSideDataFFmpeg.h"
#include "lavf_log.h"

HRESULT CFMS30MonoAudioMixer::Deliver(BufferDetails &buffer)
{
	HRESULT hr = S_OK;

	if (m_bFlushing)
		return S_FALSE;

	CMediaType mt = CreateMediaType(buffer.sfFormat, buffer.dwSamplesPerSec, buffer.wChannels, buffer.dwChannelMask, buffer.wBitsPerSample);
	WAVEFORMATEX* wfe = (WAVEFORMATEX*)mt.Format();

	IMediaSample *pOut;
	BYTE *pDataOut = nullptr;
	if (FAILED(GetDeliveryBuffer(&pOut, &pDataOut))) {
		return E_FAIL;
	}

	if (m_bResyncTimestamp && buffer.rtStart != AV_NOPTS_VALUE) {
		m_rtStart = buffer.rtStart;
		m_bResyncTimestamp = FALSE;
	}

	// Length of the current sample
	double dDuration = (double)buffer.nSamples / buffer.dwSamplesPerSec * DBL_SECOND_MULT / m_dRate;
	m_dStartOffset += fmod(dDuration, 1.0);

	// Delivery Timestamps
	REFERENCE_TIME rtStart = m_rtStart, rtStop = m_rtStart + (REFERENCE_TIME)(dDuration + 0.5);

	// Compute next start time
	m_rtStart += (REFERENCE_TIME)dDuration;
	// If the offset reaches one (100ns), add it to the next frame
	if (m_dStartOffset > 0.5) {
		m_rtStart++;
		m_dStartOffset -= 1.0;
	}

	if (rtStart < 0) {
		goto done;
	}

	pOut->SetTime(&rtStart, &rtStop);
	pOut->SetMediaTime(nullptr, nullptr);

	pOut->SetPreroll(FALSE);
	pOut->SetDiscontinuity(m_bDiscontinuity);
	m_bDiscontinuity = FALSE;
	pOut->SetSyncPoint(TRUE);

	pOut->SetActualDataLength(buffer.bBuffer->GetCount());

	memcpy(pDataOut, buffer.bBuffer->Ptr(), buffer.bBuffer->GetCount());

	hr = m_pOutput->Deliver(pOut);
	if (FAILED(hr)) {
		DbgLog((LOG_ERROR, 10, L"::Deliver failed with code: %0#.8x", hr));
	}
done:
	SafeRelease(&pOut);
	return hr;
}


HRESULT CFMS30MonoAudioMixer::ProcessBufferLR()
{
	HRESULT hr = S_OK;
	if (m_buff.GetCount() <= 0 || m_buffR.GetCount() <= 0)
	{
		//both two buffers must have data 
		return S_OK;
	}

	int nToSend = min(m_buff.GetCount(), m_buffR.GetCount());

	//mix buff
	GrowableArray<BYTE> m_mixBuff;
	m_mixBuff.Allocate(nToSend * 2);
	int nOneChnBlock = m_wavFmt.nBlockAlign / 2;
	int nSamples = nToSend / nOneChnBlock;

	int dwTotalBlock = nToSend / nOneChnBlock;
	BYTE* pLeftPtr = m_buff.Ptr();
	BYTE* pRightPtr = m_buffR.Ptr();

	for (int i = 0; i < dwTotalBlock; ++i)
	{
		m_mixBuff.Append(pLeftPtr, nOneChnBlock);
		m_mixBuff.Append(pRightPtr, nOneChnBlock);
		pLeftPtr += nOneChnBlock;
		pRightPtr += nOneChnBlock;
	}

	//delete buff

	m_buff.Consume(nToSend);
	m_buffR.Consume(nToSend);

	//to send
	IMediaSample *pOut;
	BYTE *pDataOut = nullptr;
	if (FAILED(GetDeliveryBuffer(&pOut, &pDataOut))) {
		return E_FAIL;
	}

	//
	double dDuration = (double)nSamples / m_wavFmt.nSamplesPerSec * DBL_SECOND_MULT / m_dRate;
	m_dStartOffset += fmod(dDuration, 1.0);
	// Delivery Timestamps
	REFERENCE_TIME rtStart = m_rtStart, rtStop = m_rtStart + (REFERENCE_TIME)(dDuration + 0.5);
	// Compute next start time
	m_rtStart += (REFERENCE_TIME)dDuration;
	// If the offset reaches one (100ns), add it to the next frame
	if (m_dStartOffset > 0.5) {
		m_rtStart++;
		m_dStartOffset -= 1.0;
	}

	if (rtStart < 0) {
		goto done;
	}

	pOut->SetTime(&rtStart, &rtStop);
	pOut->SetMediaTime(nullptr, nullptr);

	pOut->SetPreroll(FALSE);
	pOut->SetDiscontinuity(m_bDiscontinuity);
	m_bDiscontinuity = FALSE;
	pOut->SetSyncPoint(TRUE);

	pOut->SetActualDataLength(m_mixBuff.GetCount());

	memcpy(pDataOut, m_mixBuff.Ptr(), m_mixBuff.GetCount());

	hr = m_pOutput->Deliver(pOut);
	if (FAILED(hr)) {
		DbgLog((LOG_ERROR, 10, L"::Deliver failed with code: %0#.8x", hr));
	}


done:
	SafeRelease(&pOut);
	return hr;

}

HRESULT CFMS30MonoAudioMixer::PerformFlush()
{
	CAutoLock cAutoLock(&m_csReceive);

	m_buff.Clear();
	FlushOutput(FALSE);
	FlushDecoder();

	m_rtStart = 0;
	m_bQueueResync = TRUE;

	return S_OK;
}


CFMS30MonoAudioMixer::CFMS30MonoAudioMixer(LPUNKNOWN pUnk, HRESULT * phr)
	:CTransformFilter(NAME("Fms30 monoAudio Mixer"), 0, __uuidof(CFMS30MonoAudioMixer))
{
	StaticInit(TRUE, nullptr);
	CMonoAudioInputPin* pTmpPin1 = new CMonoAudioInputPin(TEXT("CDeCSSTransformInputPin"), this, phr, L"Input");
	if (!pTmpPin1) {
		*phr = E_OUTOFMEMORY;
	}
	if (FAILED(*phr)) {
		return;
	}

	pTmpPin1->SetAudioChannelPos(AudioChn_left);
	pTmpPin1->SetMonoAReceiveCallback(this);
	m_pInput = pTmpPin1;

	m_vInputPin.push_back(pTmpPin1);

	CMonoAudioInputPin *m_pInput2 = new CMonoAudioInputPin(TEXT("CDeCSSTransformInputPin"), this, phr, L"Input");
	if (!m_pInput2) {
		*phr = E_OUTOFMEMORY;
	}
	if (FAILED(*phr)) {
		return;
	}

	m_pInput2->SetAudioChannelPos(AudioChn_right);
	m_pInput2->SetMonoAReceiveCallback(this);
	m_vInputPin.push_back(m_pInput2);

	m_pOutput = new CTransformOutputPin(NAME("CTransformOutputPin"), this, phr, L"Output");
	if (!m_pOutput) {
		*phr = E_OUTOFMEMORY;
	}
	if (FAILED(*phr)) {
		SAFE_DELETE(m_pInput);
		return;
	}

	//InitBitstreaming();

#ifdef DEBUG
	DbgSetModuleLevel(LOG_CUSTOM1, DWORD_MAX); // FFMPEG messages use custom1
	av_log_set_callback(lavf_log_callback);

	DbgSetModuleLevel(LOG_ERROR, DWORD_MAX);
	DbgSetModuleLevel(LOG_TRACE, DWORD_MAX);
	//DbgSetModuleLevel (LOG_CUSTOM2, DWORD_MAX); // Jitter statistics
	//DbgSetModuleLevel (LOG_CUSTOM5, DWORD_MAX); // Extensive timing options

#ifdef LAV_DEBUG_RELEASE
	DbgSetLogFileDesktop(LAVC_AUDIO_LOG_FILE);
#endif
#else
	av_log_set_callback(nullptr);
#endif
}

CFMS30MonoAudioMixer::~CFMS30MonoAudioMixer()
{

	//删除第二个inputpin, 第一个pin在基类删除了
	if (IPin* pPinTo = m_vInputPin[1]->GetConnected())
	{
		pPinTo->Disconnect();
	}
	m_vInputPin[1]->Disconnect();
	SAFE_DELETE(m_vInputPin[1]);

}

HRESULT CFMS30MonoAudioMixer::OnAudioReceive(IMediaSample * pIn, EnumAudioCHNPos apos)
{
	CAutoLock cAutoLock(&m_csReceive);

	HRESULT hr;

	CTransformInputPin *pInput = m_vInputPin[apos];


	AM_SAMPLE2_PROPERTIES const *pProps = pInput->SampleProps();
	if (pProps->dwStreamId != AM_STREAM_MEDIA) {
		return m_pOutput->Deliver(pIn);
	}


	BYTE *pDataIn = nullptr;
	if (FAILED(hr = pIn->GetPointer(&pDataIn))) {
		return hr;
	}

	long len = pIn->GetActualDataLength();
	if (len < 0) {
		DbgLog((LOG_ERROR, 10, L"Invalid data length, aborting"));
		return E_FAIL;
	}
	else if (len == 0) {
		return S_OK;
	}

	GrowableArray<BYTE>& buffRef = (apos == AudioChn_left) ? m_buff : m_buffR;

	REFERENCE_TIME rtStart = _I64_MIN, rtStop = _I64_MIN;
	hr = pIn->GetTime(&rtStart, &rtStop);

	if ((pIn->IsDiscontinuity() == S_OK || (m_bNeedSyncpoint && pIn->IsSyncPoint() == S_OK))) {
		DbgLog((LOG_ERROR, 10, L"::Receive(): Discontinuity, flushing decoder.."));
		m_buff.Clear();
		m_buffR.Clear();
		m_bQueueResync = TRUE;
		m_bDiscontinuity = TRUE;
		if (FAILED(hr)) {
			DbgLog((LOG_ERROR, 10, L" -> Discontinuity without timestamp"));
		}

		if (m_bNeedSyncpoint && pIn->IsSyncPoint() == S_OK) {
			DbgLog((LOG_TRACE, 10, L"::Receive(): Got SyncPoint, resuming decoding...."));
			m_bNeedSyncpoint = FALSE;
		}
	}

	if (m_bQueueResync && SUCCEEDED(hr)) {
		DbgLog((LOG_TRACE, 10, L"Resync Request; old: %I64d; new: %I64d; buffer: %d", m_rtStart, rtStart, m_buff.GetCount()));
		ProcessBufferLR();
		if (m_rtStart != AV_NOPTS_VALUE && rtStart != m_rtStart)
			m_bDiscontinuity = TRUE;

		m_rtStart = rtStart;
		m_rtStartInputCache = AV_NOPTS_VALUE;
		m_rtBitstreamCache = AV_NOPTS_VALUE;
		m_dStartOffset = 0.0;
		m_bQueueResync = FALSE;
		m_bResyncTimestamp = TRUE;
	}

	m_bJustFlushed = FALSE;

	m_rtStartInput = SUCCEEDED(hr) ? rtStart : AV_NOPTS_VALUE;
	m_rtStopInput = SUCCEEDED(hr) ? rtStop : AV_NOPTS_VALUE;

	DWORD bufflen = buffRef.GetCount();

	// Ensure the size of the buffer doesn't overflow (its used as signed int in various places)
	if (bufflen > (INT_MAX - (DWORD)len)) {
		DbgLog((LOG_TRACE, 10, L"Too much audio buffered, aborting"));
		m_buff.Clear();
		m_buffR.Clear();
		m_bQueueResync = TRUE;
		return E_FAIL;
	}

	buffRef.Allocate(bufflen + len + FF_INPUT_BUFFER_PADDING_SIZE);
	buffRef.Append(pDataIn, len);

	//
	hr = ProcessBufferLR();

	if (FAILED(hr))
		return hr;

	return S_OK;
}

void CALLBACK CFMS30MonoAudioMixer::StaticInit(BOOL bLoading, const CLSID *clsid)
{
	if (!bLoading) return;

	av_register_all();
}

STDMETHODIMP CFMS30MonoAudioMixer::NonDelegatingQueryInterface(REFIID riid, void ** ppv)
{
	CheckPointer(ppv, E_POINTER);

	*ppv = nullptr;

	return
		__super::NonDelegatingQueryInterface(riid, ppv);
}

int CFMS30MonoAudioMixer::GetPinCount()
{
	CAutoLock lock(&m_csPins);

	int count = (int)m_vInputPin.size();
	if (m_pOutput)
		count++;

	return count;
}

CBasePin * CFMS30MonoAudioMixer::GetPin(int n)
{
	CAutoLock lock(&m_csPins);

	if (n < 0 || n >= GetPinCount()) return nullptr;


	if (n == 2)
	{
		if (m_pOutput)
		{
			return m_pOutput;
		}

	}
	else
	{

	}

	return (CBasePin *)m_vInputPin[n];
}

HRESULT CFMS30MonoAudioMixer::CheckInputType(const CMediaType * mtIn)
{
	for (UINT i = 0; i < sudPinTypesInCount; i++) {
		if (*sudPinTypesIn[i].clsMajorType == mtIn->majortype
			&& *sudPinTypesIn[i].clsMinorType == mtIn->subtype && (mtIn->formattype == FORMAT_WaveFormatEx)) {

			return S_OK;
		}
	}

	return VFW_E_TYPE_NOT_ACCEPTED;
}

HRESULT CFMS30MonoAudioMixer::CheckTransform(const CMediaType * mtIn, const CMediaType * mtOut)
{
	// Check major types
	if (FAILED(CheckInputType(mtIn)) || mtOut->majortype != MEDIATYPE_Audio || (mtOut->subtype != MEDIASUBTYPE_PCM && mtOut->subtype != MEDIASUBTYPE_IEEE_FLOAT)
		|| mtOut->formattype != FORMAT_WaveFormatEx) {
		return VFW_E_TYPE_NOT_ACCEPTED;
	}

	return S_OK;
}

HRESULT CFMS30MonoAudioMixer::DecideBufferSize(IMemAllocator * pAllocator, ALLOCATOR_PROPERTIES *pProperties)
{
	if (m_pInput->IsConnected() == FALSE) {
		return E_UNEXPECTED;
	}

	/*CMediaType& mt = m_pInput->CurrentMediaType();
	WAVEFORMATEX* wfe = (WAVEFORMATEX*)mt.Format();
	UNUSED_ALWAYS(wfe); */

	pProperties->cBuffers = 4;
	// TODO: we should base this on the output media type
	pProperties->cbBuffer = FMS_AUDIO_BUFFER_SIZE; // 48KHz 6ch 32bps 100ms
	pProperties->cbAlign = 1;
	pProperties->cbPrefix = 0;

	HRESULT hr;
	ALLOCATOR_PROPERTIES Actual;
	if (FAILED(hr = pAllocator->SetProperties(pProperties, &Actual))) {
		return hr;
	}

	return pProperties->cBuffers > Actual.cBuffers || pProperties->cbBuffer > Actual.cbBuffer
		? E_FAIL
		: NOERROR;
}

HRESULT CFMS30MonoAudioMixer::GetMediaType(int iPosition, CMediaType * pMediaType)
{
	DbgLog((LOG_TRACE, 5, L"GetMediaType"));
	if (m_pInput->IsConnected() == FALSE) {
		return E_UNEXPECTED;
	}

	if (iPosition < 0) {
		return E_INVALIDARG;
	}
	if (iPosition > 0) {
		return VFW_S_NO_MORE_ITEMS;
	}


	*pMediaType = m_outputMediaType;

	return S_OK;
}

HRESULT CFMS30MonoAudioMixer::Receive(IMediaSample * pIn)
{
	return E_NOTIMPL;
}

HRESULT CFMS30MonoAudioMixer::CompleteConnect(PIN_DIRECTION direction, IPin * pReceivePin)
{
	//get meida type from input pin
	if (direction == PINDIR_INPUT)
	{
		CMediaType mtype;
		pReceivePin->ConnectionMediaType(&mtype);

		m_outputMediaType.Set(mtype);
		m_outputMediaType.lSampleSize *= 2;

		WAVEFORMATEX* pFmt = (WAVEFORMATEX*)mtype.Format();
		WAVEFORMATEXTENSIBLE* pwfex = (WAVEFORMATEXTENSIBLE*)mtype.Format();

		pFmt->nAvgBytesPerSec *= 2;
		pFmt->nBlockAlign *= 2;
		pFmt->nChannels = 2;
		
		//pFmt->nSamplesPerSec
		//pFmt->wBitsPerSample;
		if (pFmt->wFormatTag == 65534 && pFmt->cbSize >= 22)
		{
			pwfex->Samples.wReserved = 0;
			pwfex->dwChannelMask = (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
			pwfex->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
		}
		m_wavFmt = *pFmt;
		m_outputMediaType.SetFormat((BYTE*)pwfex, mtype.FormatLength());
	}



	return NOERROR;
}

STDMETHODIMP CFMS30MonoAudioMixer::JoinFilterGraph(IFilterGraph * pGraph, LPCWSTR pName)
{
	CAutoLock cObjectLock(m_pLock);
	HRESULT hr = __super::JoinFilterGraph(pGraph, pName);
	return hr;
}

HRESULT CFMS30MonoAudioMixer::CheckConnect(PIN_DIRECTION dir, IPin * pPin)
{
	return __super::CheckConnect(dir, pPin);
}

HRESULT CFMS30MonoAudioMixer::SetMediaType(PIN_DIRECTION dir, const CMediaType * pmt)
{
	DbgLog((LOG_TRACE, 5, L"SetMediaType -- %S", dir == PINDIR_INPUT ? "in" : "out"));
	/*if (dir == PINDIR_INPUT) {
		AVCodecID codec = AV_CODEC_ID_NONE;
		const void *format = pmt->Format();
		GUID format_type = pmt->formattype;
		DWORD formatlen = pmt->cbFormat;

		// Override the format type
		if (pmt->subtype == MEDIASUBTYPE_FFMPEG_AUDIO && pmt->formattype == FORMAT_WaveFormatExFFMPEG) {
			WAVEFORMATEXFFMPEG *wfexff = (WAVEFORMATEXFFMPEG *)pmt->Format();
			codec = (AVCodecID)wfexff->nCodecId;
			format = &wfexff->wfex;
			format_type = FORMAT_WaveFormatEx;
			formatlen -= sizeof(WAVEFORMATEXFFMPEG) - sizeof(WAVEFORMATEX);
		}
		else {
			codec = FindCodecId(pmt);
		}

		if (codec == AV_CODEC_ID_NONE)
			return VFW_E_TYPE_NOT_ACCEPTED;

		HRESULT hr = ffmpeg_init(codec, format, format_type, formatlen);
		if (FAILED(hr)) {
			return hr;
		}

	}*/
	return __super::SetMediaType(dir, pmt);
}

HRESULT CFMS30MonoAudioMixer::EndOfStream()
{
	DbgLog((LOG_TRACE, 10, L"CFMS30Audio::EndOfStream()"));
	CAutoLock cAutoLock(&m_csReceive);

	// Flush the last data out of the parser
	ProcessBufferLR();
	// 	ProcessBuffer(nullptr);
	// 	ProcessBuffer(nullptr, TRUE);

	//	FlushOutput(TRUE);
	return __super::EndOfStream();
}

HRESULT CFMS30MonoAudioMixer::BeginFlush()
{
	DbgLog((LOG_TRACE, 10, L"CFMS30Audio::BeginFlush()"));
	m_bFlushing = TRUE;
	return __super::BeginFlush();
}

HRESULT CFMS30MonoAudioMixer::EndFlush()
{
	DbgLog((LOG_TRACE, 10, L"CFMS30Audio::EndFlush()"));
	CAutoLock cAutoLock(&m_csReceive);

	HRESULT hr = __super::EndFlush();
	m_bFlushing = FALSE;
	return hr;
}

HRESULT CFMS30MonoAudioMixer::NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate)
{
	DbgLog((LOG_TRACE, 10, L"CFMS30Audio::NewSegment() tStart: %I64d, tStop: %I64d, dRate: %.2f", tStart, tStop, dRate));
	CAutoLock cAutoLock(&m_csReceive);

	//PerformFlush();

	if (dRate > 0.0)
		m_dRate = dRate;
	else
		m_dRate = 1.0;
	return __super::NewSegment(tStart, tStop, dRate);
}

HRESULT CFMS30MonoAudioMixer::BreakConnect(PIN_DIRECTION Dir)
{
	return __super::BreakConnect(Dir);
}

CMediaType CFMS30MonoAudioMixer::CreateMediaType(LAVAudioSampleFormat outputFormat, DWORD nSamplesPerSec, WORD nChannels, DWORD dwChannelMask, WORD wBitsPerSample) const
{
	throw(E_NOTIMPL);
}

HRESULT CFMS30MonoAudioMixer::ProcessBuffer(IMediaSample * pMediaSample, BOOL bEOF)
{
	return S_OK;
}


static DWORD get_lav_channel_layout(uint64_t layout)
{
	if (layout > UINT32_MAX) {
		if (layout & AV_CH_WIDE_LEFT)
			layout = (layout & ~AV_CH_WIDE_LEFT) | AV_CH_FRONT_LEFT_OF_CENTER;
		if (layout & AV_CH_WIDE_RIGHT)
			layout = (layout & ~AV_CH_WIDE_RIGHT) | AV_CH_FRONT_RIGHT_OF_CENTER;

		if (layout & AV_CH_SURROUND_DIRECT_LEFT)
			layout = (layout & ~AV_CH_SURROUND_DIRECT_LEFT) | AV_CH_SIDE_LEFT;
		if (layout & AV_CH_SURROUND_DIRECT_RIGHT)
			layout = (layout & ~AV_CH_SURROUND_DIRECT_RIGHT) | AV_CH_SIDE_RIGHT;
	}

	return (DWORD)layout;
}


HRESULT CFMS30MonoAudioMixer::Decode(const BYTE *pDataBuffer, int buffsize, int &consumed, HRESULT *hrDeliver, IMediaSample *pMediaSample)
{
	return S_OK;
}

HRESULT CFMS30MonoAudioMixer::PostProcess(BufferDetails * buffer)
{
	return S_OK;
}

HRESULT CFMS30MonoAudioMixer::GetDeliveryBuffer(IMediaSample **pSample, BYTE **pData)
{

	HRESULT hr;

	*pData = nullptr;
	if (FAILED(hr = m_pOutput->GetDeliveryBuffer(pSample, nullptr, nullptr, 0))
		|| FAILED(hr = (*pSample)->GetPointer(pData))) {
		return hr;
	}

/*
	AM_MEDIA_TYPE* pmt = nullptr;
	if (SUCCEEDED((*pSample)->GetMediaType(&pmt)) && pmt) {
		CMediaType mt = *pmt;
		m_pOutput->SetMediaType(&mt);
		DeleteMediaType(pmt);
		pmt = nullptr;
	}*/

	return S_OK;
}

HRESULT CFMS30MonoAudioMixer::QueueOutput(BufferDetails & buffer)
{
	return S_OK;
}

HRESULT CFMS30MonoAudioMixer::FlushOutput(BOOL bDeliver)
{
	return S_OK;
}

HRESULT CFMS30MonoAudioMixer::FlushDecoder()
{

	return S_OK;
}

