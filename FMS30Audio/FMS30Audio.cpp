// FMS30Audio.cpp : 定义 DLL 应用程序的导出函数。
//

#include "stdafx.h"
#include "FMS30Audio.h"

#include <MMReg.h>
#include <assert.h>

#include "moreuuids.h"
#include "IMediaSideData.h"
#include "IMediaSideDataFFmpeg.h"

#include "DeCSS/DeCSSInputPin.h"
#include "lavf_log.h"

void CALLBACK CFMS30Audio::StaticInit(BOOL bLoading, const CLSID *clsid)
{
	if (!bLoading) return;

	av_register_all();
}

CFMS30Audio::CFMS30Audio(LPUNKNOWN pUnk, HRESULT* phr)
	:CTransformFilter(NAME("Fms30 audio decoder"), 0, __uuidof(CFMS30Audio))
{
	StaticInit(TRUE, nullptr);

	m_pInput = new CDeCSSTransformInputPin(TEXT("CDeCSSTransformInputPin"), this, phr, L"Input");
	if (!m_pInput) {
		*phr = E_OUTOFMEMORY;
	}
	if (FAILED(*phr)) {
		return;
	}

	m_pOutput = new CTransformOutputPin(NAME("CTransformOutputPin"), this, phr, L"Output");
	if (!m_pOutput) {
		*phr = E_OUTOFMEMORY;
	}
	if (FAILED(*phr)) {
		SAFE_DELETE(m_pInput);
		return;
	}

	//LoadSettings();
	m_settings.OutputStandardLayout = TRUE;
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


CFMS30Audio::~CFMS30Audio()
{
	ffmpeg_shutdown();

#if defined(DEBUG) && defined(LAV_DEBUG_RELEASE)
	DbgCloseLogFile();
#endif
}

STDMETHODIMP CFMS30Audio::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
	CheckPointer(ppv, E_POINTER);

	*ppv = nullptr;

	return
		QI2(ILAVAudioSettings)
		__super::NonDelegatingQueryInterface(riid, ppv);
}

STDMETHODIMP CFMS30Audio::SetOutputStandardLayout(BOOL bStdLayout)
{
	m_settings.OutputStandardLayout = bStdLayout;
	return S_OK;
}

STDMETHODIMP_(BOOL) CFMS30Audio::GetOutputStandardLayout()
{
	return m_settings.OutputStandardLayout;
}

HRESULT CFMS30Audio::CheckInputType(const CMediaType* mtIn)
{
	for (UINT i = 0; i < sudPinTypesInCount; i++) {
		if (*sudPinTypesIn[i].clsMajorType == mtIn->majortype
			&& *sudPinTypesIn[i].clsMinorType == mtIn->subtype && (mtIn->formattype == FORMAT_WaveFormatEx || mtIn->formattype == FORMAT_WaveFormatExFFMPEG || mtIn->formattype == FORMAT_VorbisFormat2)) {
			return S_OK;
		}
	}

	return VFW_E_TYPE_NOT_ACCEPTED;
}

HRESULT CFMS30Audio::CheckTransform(const CMediaType* mtIn, const CMediaType* mtOut)
{
	// Check major types
	if (FAILED(CheckInputType(mtIn)) || mtOut->majortype != MEDIATYPE_Audio || (mtOut->subtype != MEDIASUBTYPE_PCM && mtOut->subtype != MEDIASUBTYPE_IEEE_FLOAT)
		|| mtOut->formattype != FORMAT_WaveFormatEx) {
		return VFW_E_TYPE_NOT_ACCEPTED;
	}

return S_OK;
}

HRESULT CFMS30Audio::DecideBufferSize(IMemAllocator * pAllocator, ALLOCATOR_PROPERTIES *pProperties)
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

HRESULT CFMS30Audio::GetMediaType(int iPosition, CMediaType *pMediaType)
{
	DbgLog((LOG_TRACE, 5, L"GetMediaType"));
	if (m_pInput->IsConnected() == FALSE || !((m_pAVCtx && m_pAVCodec))) {
		return E_UNEXPECTED;
	}

	if (iPosition < 0) {
		return E_INVALIDARG;
	}

	const int nSamplesPerSec = m_pAVCtx->sample_rate;
	int nChannels = m_pAVCtx->channels;
	DWORD dwChannelMask = get_channel_mask(nChannels);

	AVSampleFormat sample_fmt = (m_pAVCtx->sample_fmt != AV_SAMPLE_FMT_NONE) ? m_pAVCtx->sample_fmt : (m_pAVCodec->sample_fmts ? m_pAVCodec->sample_fmts[0] : AV_SAMPLE_FMT_NONE);
	if (sample_fmt == AV_SAMPLE_FMT_NONE)
		sample_fmt = AV_SAMPLE_FMT_S32; // this gets mapped to S16/S24/S32 in get_lav_sample_fmt based on the bits per sample

										// Prefer bits_per_raw_sample if set, but if not, try to do a better guess with bits per coded sample
	int bits = m_pAVCtx->bits_per_raw_sample ? m_pAVCtx->bits_per_raw_sample : m_pAVCtx->bits_per_coded_sample;

	LAVAudioSampleFormat lav_sample_fmt;
	lav_sample_fmt = get_lav_sample_fmt(sample_fmt, bits);

	if (iPosition > 1)
		return VFW_S_NO_MORE_ITEMS;

	if (iPosition % 2) {
		lav_sample_fmt = SampleFormat_16;
		bits = 16;
	}
	else {
		lav_sample_fmt = GetBestAvailableSampleFormat(lav_sample_fmt, &bits, TRUE);
	}

	*pMediaType = CreateMediaType(lav_sample_fmt, nSamplesPerSec, nChannels, dwChannelMask, bits);
	
	return S_OK;
}

HRESULT CFMS30Audio::Receive(IMediaSample *pIn)
{
	CAutoLock cAutoLock(&m_csReceive);

	HRESULT hr;

	AM_SAMPLE2_PROPERTIES const *pProps = m_pInput->SampleProps();
	if (pProps->dwStreamId != AM_STREAM_MEDIA) {
		return m_pOutput->Deliver(pIn);
	}

	if (!m_pAVCtx) {
		return E_FAIL;
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

	(static_cast<CDeCSSTransformInputPin*>(m_pInput))->StripPacket(pDataIn, len);

	REFERENCE_TIME rtStart = _I64_MIN, rtStop = _I64_MIN;
	hr = pIn->GetTime(&rtStart, &rtStop);

	if ((pIn->IsDiscontinuity() == S_OK || (m_bNeedSyncpoint && pIn->IsSyncPoint() == S_OK))) {
		DbgLog((LOG_ERROR, 10, L"::Receive(): Discontinuity, flushing decoder.."));
		m_buff.Clear();
		FlushOutput(FALSE);
		FlushDecoder();
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
		FlushOutput();
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

	DWORD bufflen = m_buff.GetCount();

	// Ensure the size of the buffer doesn't overflow (its used as signed int in various places)
	if (bufflen > (INT_MAX - (DWORD)len)) {
		DbgLog((LOG_TRACE, 10, L"Too much audio buffered, aborting"));
		m_buff.Clear();
		m_bQueueResync = TRUE;
		return E_FAIL;
	}

	m_buff.Allocate(bufflen + len + AV_INPUT_BUFFER_PADDING_SIZE);
	m_buff.Append(pDataIn, len);

	hr = ProcessBuffer(pIn);

	if (FAILED(hr))
		return hr;

	return S_OK;
}

STDMETHODIMP CFMS30Audio::JoinFilterGraph(IFilterGraph * pGraph, LPCWSTR pName)
{
	CAutoLock cObjectLock(m_pLock);
	HRESULT hr = __super::JoinFilterGraph(pGraph, pName);
	return hr;
}

HRESULT CFMS30Audio::CheckConnect(PIN_DIRECTION dir, IPin *pPin)
{
	return __super::CheckConnect(dir, pPin);
}

HRESULT CFMS30Audio::SetMediaType(PIN_DIRECTION dir, const CMediaType *pmt)
{
	DbgLog((LOG_TRACE, 5, L"SetMediaType -- %S", dir == PINDIR_INPUT ? "in" : "out"));
	if (dir == PINDIR_INPUT) {
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

	}
	return __super::SetMediaType(dir, pmt);
}

HRESULT CFMS30Audio::EndOfStream()
{
	DbgLog((LOG_TRACE, 10, L"CFMS30Audio::EndOfStream()"));
	CAutoLock cAutoLock(&m_csReceive);

	// Flush the last data out of the parser
	ProcessBuffer(nullptr);
	ProcessBuffer(nullptr, TRUE);

	FlushOutput(TRUE);
	return __super::EndOfStream();
}

HRESULT CFMS30Audio::BeginFlush()
{
	DbgLog((LOG_TRACE, 10, L"CFMS30Audio::BeginFlush()"));
	m_bFlushing = TRUE;
	return __super::BeginFlush();
}

HRESULT CFMS30Audio::EndFlush()
{
	DbgLog((LOG_TRACE, 10, L"CFMS30Audio::EndFlush()"));
	CAutoLock cAutoLock(&m_csReceive);

	HRESULT hr = __super::EndFlush();
	m_bFlushing = FALSE;
	return hr;
}

HRESULT CFMS30Audio::NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate)
{
	DbgLog((LOG_TRACE, 10, L"CFMS30Audio::NewSegment() tStart: %I64d, tStop: %I64d, dRate: %.2f", tStart, tStop, dRate));
	CAutoLock cAutoLock(&m_csReceive);

	PerformFlush();

	if (dRate > 0.0)
		m_dRate = dRate;
	else
		m_dRate = 1.0;
	return __super::NewSegment(tStart, tStop, dRate);
}

HRESULT CFMS30Audio::BreakConnect(PIN_DIRECTION dir)
{
	if (dir == PINDIR_INPUT) {
		ffmpeg_shutdown();
	}
	return __super::BreakConnect(dir);
}

HRESULT CFMS30Audio::ffmpeg_init(AVCodecID codec, const void *format, GUID format_type, DWORD formatlen)
{
	CAutoLock lock(&m_csReceive);
	ffmpeg_shutdown();
	DbgLog((LOG_TRACE, 10, L"::ffmpeg_init(): Initializing decoder for codec %S", avcodec_get_name(codec)));

	//检查是否有相应的解码器
	for (int i = 0; i < Codec_AudioNB; ++i) {
		const codec_config_t *config = get_codec_config((LAVAudioCodec)i);
		bool bMatched = false;
		for (int k = 0; k < config->nCodecs; ++k) {
			if (config->codecs[k] == codec) {
				bMatched = true;
				break;
			}
		}
	}

	m_pAVCodec = nullptr;

	m_pAVCodec = avcodec_find_decoder(codec);
	CheckPointer(m_pAVCodec, VFW_E_UNSUPPORTED_AUDIO);

	m_pAVCtx = avcodec_alloc_context3(m_pAVCodec);
	CheckPointer(m_pAVCtx, E_POINTER);

	DWORD nSamples, nBytesPerSec;
	WORD nChannels, nBitsPerSample, nBlockAlign;
	audioFormatTypeHandler((BYTE *)format, &format_type, &nSamples, &nChannels, &nBitsPerSample, &nBlockAlign, &nBytesPerSec);

	m_pAVCtx->thread_count = 1;
	m_pAVCtx->thread_type = 0;
	m_pAVCtx->sample_rate = nSamples;
	m_pAVCtx->channels = nChannels;
	m_pAVCtx->bit_rate = nBytesPerSec << 3;
	m_pAVCtx->bits_per_coded_sample = nBitsPerSample;
	m_pAVCtx->block_align = nBlockAlign;
	m_pAVCtx->err_recognition = 0;
	m_pAVCtx->refcounted_frames = 1;
	m_pAVCtx->pkt_timebase.num = 1;
	m_pAVCtx->pkt_timebase.den = 10000000;

	m_nCodecId = codec;

	int ret = avcodec_open2(m_pAVCtx, m_pAVCodec, nullptr);
	if (ret >= 0) {
		m_pFrame = av_frame_alloc();
	}
	else {
		return VFW_E_UNSUPPORTED_AUDIO;
	}

	m_pSwr = swr_alloc_set_opts(
			nullptr,
			m_pAVCtx->channel_layout
			? m_pAVCtx->channel_layout
			: av_get_default_channel_layout(m_pAVCtx->channels),
			AV_SAMPLE_FMT_S32,
			m_pAVCtx->sample_rate,
			m_pAVCtx->channel_layout
			? m_pAVCtx->channel_layout
			: av_get_default_channel_layout(m_pAVCtx->channels),
			m_pAVCtx->sample_fmt,
			m_pAVCtx->sample_rate,
			0,
			nullptr);

	swr_init(m_pSwr);

	return S_OK;
}

void CFMS30Audio::ffmpeg_shutdown()
{
	m_pAVCodec = nullptr;
	if (m_pAVCtx) {
		avcodec_close(m_pAVCtx);
		av_freep(&m_pAVCtx->extradata);
		av_freep(&m_pAVCtx);
	}
	av_frame_free(&m_pFrame);
	swr_free(&m_pSwr);

	m_nCodecId = AV_CODEC_ID_NONE;
}

CMediaType CFMS30Audio::CreateMediaType(LAVAudioSampleFormat outputFormat, DWORD nSamplesPerSec, WORD nChannels, DWORD dwChannelMask, WORD wBitsPerSample /*= 0*/) const
{
	CMediaType mt;

	mt.majortype = MEDIATYPE_Audio;
	mt.subtype = (outputFormat == SampleFormat_FP32) ? MEDIASUBTYPE_IEEE_FLOAT : MEDIASUBTYPE_PCM;
	mt.formattype = FORMAT_WaveFormatEx;

	WAVEFORMATEXTENSIBLE wfex;
	memset(&wfex, 0, sizeof(wfex));

	if (wBitsPerSample >> 3 > get_byte_per_sample(outputFormat)) {
		DbgLog((LOG_TRACE, 20, L"Invalid combination of sample format and bits per sample"));
		outputFormat = get_lav_sample_fmt(AV_SAMPLE_FMT_S32, wBitsPerSample);
	}

	WAVEFORMATEX* wfe = &wfex.Format;
	wfe->wFormatTag = (WORD)mt.subtype.Data1;
	wfe->nChannels = nChannels;
	wfe->nSamplesPerSec = nSamplesPerSec;
	wfe->wBitsPerSample = get_byte_per_sample(outputFormat) << 3;
	wfe->nBlockAlign = wfe->nChannels * wfe->wBitsPerSample / 8;
	wfe->nAvgBytesPerSec = wfe->nSamplesPerSec * wfe->nBlockAlign;

	if (dwChannelMask == 0 && (wfe->wBitsPerSample > 16 || wfe->nSamplesPerSec > 48000)) {
		dwChannelMask = nChannels == 2 ? (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT) : SPEAKER_FRONT_CENTER;
	}

	// Dont use a channel mask for "default" mono/stereo sources
	if ((outputFormat == SampleFormat_FP32 || wfe->wBitsPerSample <= 16) && wfe->nSamplesPerSec <= 48000 && ((nChannels == 1 && dwChannelMask == SPEAKER_FRONT_CENTER) || (nChannels == 2 && dwChannelMask == (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT)))) {
		dwChannelMask = 0;
	}

	if (dwChannelMask) {
		wfex.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
		wfex.Format.cbSize = sizeof(wfex) - sizeof(wfex.Format);
		wfex.dwChannelMask = dwChannelMask;
		if (wBitsPerSample > 0 && (outputFormat == SampleFormat_24 || outputFormat == SampleFormat_32)) {
			WORD wBpp = wBitsPerSample;
			if ((outputFormat == SampleFormat_24 && wBpp <= 16)
				|| (outputFormat == SampleFormat_32 && wBpp < 24))
				wBpp = 24;
			wfex.Samples.wValidBitsPerSample = wBpp;
		}
		else {
			wfex.Samples.wValidBitsPerSample = wfex.Format.wBitsPerSample;
		}
		wfex.SubFormat = mt.subtype;
	}

	mt.SetSampleSize(wfe->wBitsPerSample * wfe->nChannels / 8);
	mt.SetFormat((BYTE*)&wfex, sizeof(wfex.Format) + wfex.Format.cbSize);

	return mt;
}

HRESULT CFMS30Audio::ProcessBuffer(IMediaSample *pMediaSample, BOOL bEOF /*= FALSE*/)
{
	HRESULT hr = S_OK, hr2 = S_OK;

	int buffer_size = m_buff.GetCount();

	BYTE *p = m_buff.Ptr();
	int consumed = 0, consumed_header = 0;

	if (!bEOF)
	{
		//wxg20180226 暂不处理
	}
	else
	{
		p = nullptr;
		buffer_size = -1;
	}

	// Decoding
	// Consume the buffer data
	hr2 = Decode(p, buffer_size, consumed, &hr, pMediaSample);

	// FAILED - throw away the data
	if (FAILED(hr2)) {
		DbgLog((LOG_TRACE, 10, L"Dropped invalid sample in ProcessBuffer"));
		m_buff.Clear();
		m_bQueueResync = TRUE;
		return S_FALSE;
	}

	if (bEOF || consumed <= 0) {
		return hr;
	}

	// Determine actual buffer consumption
	consumed = consumed_header + min(consumed, buffer_size);

	// Remove the consumed data from the buffer
	m_buff.Consume(consumed);

	return hr;
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

HRESULT CFMS30Audio::Decode(const BYTE *pDataBuffer, int buffsize, int &consumed, HRESULT *hrDeliver, IMediaSample *pMediaSample)
{
	int got_frame = 0;
	BYTE *tmpProcessBuf = nullptr;
	HRESULT hr = S_FALSE;

	BOOL bFlush = (pDataBuffer == nullptr);

	AVPacket avpkt;
	av_init_packet(&avpkt);

	BufferDetails out;
	const MediaSideDataFFMpeg *pFFSideData = nullptr;

	if (pMediaSample) {
		IMediaSideData *pSideData = nullptr;
		if (SUCCEEDED(pMediaSample->QueryInterface(&pSideData))) {
			size_t nFFSideDataSize = 0;
			if (FAILED(pSideData->GetSideData(IID_MediaSideDataFFMpeg, (const BYTE **)&pFFSideData, &nFFSideDataSize)) || nFFSideDataSize != sizeof(MediaSideDataFFMpeg)) {
				pFFSideData = nullptr;
			}
			SafeRelease(&pSideData);
		}
	}

	consumed = 0;
	while (buffsize > 0 || bFlush) {
		got_frame = 0;
		if (bFlush) buffsize = 0;
		if (bFlush)
		{
			hr = S_FALSE;
			break;
		}
		else
		{
			avpkt.data = (uint8_t *)pDataBuffer;
			avpkt.size = buffsize;
			avpkt.dts = m_rtStartInput;

			CopyMediaSideDataFF(&avpkt, &pFFSideData);

			int used_bytes = avcodec_decode_audio4(m_pAVCtx, m_pFrame, &got_frame, &avpkt);

			if (used_bytes < 0) {
				av_packet_unref(&avpkt);
				goto fail;
			}
			else if (used_bytes == 0 && !got_frame) {
				DbgLog((LOG_TRACE, 50, L"::Decode() - could not process buffer, starving?"));
				av_packet_unref(&avpkt);
				break;
			}
			buffsize -= used_bytes;
			pDataBuffer += used_bytes;
			consumed += used_bytes;

			// Send current input time to the delivery function
			out.rtStart = m_pFrame->pkt_dts;
			m_rtStartInput = AV_NOPTS_VALUE;
		}

		av_packet_unref(&avpkt);

		// Channel re-mapping and sample format conversion
		if (got_frame) {
			ASSERT(m_pFrame->nb_samples > 0);
			out.wChannels = m_pAVCtx->channels;
			out.dwSamplesPerSec = m_pAVCtx->sample_rate;
			if (m_pAVCtx->channel_layout)
				out.dwChannelMask = get_lav_channel_layout(m_pAVCtx->channel_layout);
			else
				out.dwChannelMask = get_channel_mask(out.wChannels);

			out.nSamples = m_pFrame->nb_samples;
			DWORD dwPCMSize = out.nSamples * out.wChannels * av_get_bytes_per_sample(AV_SAMPLE_FMT_S32);  //音频AV_SAMPLE_FMT_S32
			DWORD dwPCMSizeAligned = FFALIGN(out.nSamples, 32) * out.wChannels * av_get_bytes_per_sample(AV_SAMPLE_FMT_S32);

			if (m_pFrame->decode_error_flags & FF_DECODE_ERROR_INVALID_BITSTREAM) {
				if (m_DecodeLayout != out.dwChannelMask) {
					DbgLog((LOG_TRACE, 50, L"::Decode() - Corrupted audio frame with channel layout change, dropping."));
					av_frame_unref(m_pFrame);
					continue;
				}
			}

			out.bBuffer->Allocate(dwPCMSizeAligned);
			uint8_t *out0[] = { reinterpret_cast<uint8_t*>(out.bBuffer->Ptr()) };
			const uint8_t **in = const_cast<const uint8_t**>(m_pFrame->extended_data);

			const auto channel_samples = swr_convert(
				m_pSwr,
				out0,
				static_cast<int>(dwPCMSize) / m_pAVCtx->channels,
				in,
				m_pFrame->nb_samples);

			//这帧音频数据已经转换到out里面了
			//out.bBuffer->Append(out0[0], dwPCMSize);
			out.bBuffer->SetSize(dwPCMSize);
			out.sfFormat = SampleFormat_32;
			out.wBitsPerSample = get_byte_per_sample(SampleFormat_32) << 3;

			av_frame_unref(m_pFrame);
			hr = S_OK;


			m_DecodeFormat = out.sfFormat == SampleFormat_32 && out.wBitsPerSample > 0 && out.wBitsPerSample <= 24 ? (out.wBitsPerSample <= 16 ? SampleFormat_16 : SampleFormat_24) : out.sfFormat;
			m_DecodeLayout = out.dwChannelMask;

			if (SUCCEEDED(PostProcess(&out))) {
				*hrDeliver = QueueOutput(out);
				if (FAILED(*hrDeliver)) {
					hr = S_FALSE;
					break;
				}
			}
		}
	}
	av_free(tmpProcessBuf);
	return hr;
fail:
	av_free(tmpProcessBuf);
	return E_FAIL;
}

HRESULT CFMS30Audio::PostProcess(BufferDetails *buffer)
{
	return S_OK;
}

HRESULT CFMS30Audio::GetDeliveryBuffer(IMediaSample **pSample, BYTE **pData)
{
	HRESULT hr;

	*pData = nullptr;
	if (FAILED(hr = m_pOutput->GetDeliveryBuffer(pSample, nullptr, nullptr, 0))
		|| FAILED(hr = (*pSample)->GetPointer(pData))) {
		return hr;
	}

	AM_MEDIA_TYPE* pmt = nullptr;
	if (SUCCEEDED((*pSample)->GetMediaType(&pmt)) && pmt) {
		CMediaType mt = *pmt;
		m_pOutput->SetMediaType(&mt);
		DeleteMediaType(pmt);
		pmt = nullptr;
	}

	return S_OK;
}

HRESULT CFMS30Audio::QueueOutput(BufferDetails &buffer)
{
	HRESULT hr = S_OK;
	if (m_OutputQueue.wChannels != buffer.wChannels || m_OutputQueue.sfFormat != buffer.sfFormat || m_OutputQueue.dwSamplesPerSec != buffer.dwSamplesPerSec || m_OutputQueue.dwChannelMask != buffer.dwChannelMask || m_OutputQueue.wBitsPerSample != buffer.wBitsPerSample) {
		if (m_OutputQueue.nSamples > 0)
			FlushOutput();

		m_OutputQueue.sfFormat = buffer.sfFormat;
		m_OutputQueue.wChannels = buffer.wChannels;
		m_OutputQueue.dwChannelMask = buffer.dwChannelMask;
		m_OutputQueue.dwSamplesPerSec = buffer.dwSamplesPerSec;
		m_OutputQueue.wBitsPerSample = buffer.wBitsPerSample;
	}

	if (m_OutputQueue.nSamples == 0)
		m_OutputQueue.rtStart = buffer.rtStart;
	else if (m_OutputQueue.rtStart == AV_NOPTS_VALUE && buffer.rtStart != AV_NOPTS_VALUE)
		m_OutputQueue.rtStart = buffer.rtStart - (REFERENCE_TIME)((double)m_OutputQueue.nSamples / m_OutputQueue.dwSamplesPerSec * 10000000.0);

	// Try to retain the buffer, if possible
	if (m_OutputQueue.nSamples == 0) {
		FFSWAP(GrowableArray<BYTE>*, m_OutputQueue.bBuffer, buffer.bBuffer);
	}
	else {
		m_OutputQueue.bBuffer->Append(buffer.bBuffer);
	}
	m_OutputQueue.nSamples += buffer.nSamples;

	buffer.bBuffer->SetSize(0);
	buffer.nSamples = 0;

	// Length of the current sample
	double dDuration = (double)m_OutputQueue.nSamples / m_OutputQueue.dwSamplesPerSec * 10000000.0;
	double dOffset = fmod(dDuration, 1.0);

	// Don't exceed the buffer
	if (dDuration >= PCM_BUFFER_MAX_DURATION || (dDuration >= PCM_BUFFER_MIN_DURATION && dOffset <= FLT_EPSILON)) {
		hr = FlushOutput();
	}

	return hr;
}

HRESULT CFMS30Audio::FlushOutput(BOOL bDeliver /*= TRUE*/)
{
	CAutoLock cAutoLock(&m_csReceive);

	HRESULT hr = S_OK;
	if (bDeliver && m_OutputQueue.nSamples > 0)
		hr = Deliver(m_OutputQueue);

	// Clear Queue
	m_OutputQueue.nSamples = 0;
	m_OutputQueue.bBuffer->SetSize(0);
	m_OutputQueue.rtStart = AV_NOPTS_VALUE;

	return hr;
}

HRESULT CFMS30Audio::FlushDecoder()
{
	if (m_bJustFlushed)
		return S_OK;

	if (m_pParser) {
		av_parser_close(m_pParser);
		m_pParser = av_parser_init(m_nCodecId);
		m_bUpdateTimeCache = TRUE;
	}

	if (m_pAVCtx && avcodec_is_open(m_pAVCtx)) {
		avcodec_flush_buffers(m_pAVCtx);
	}

	m_bJustFlushed = TRUE;

	return S_OK;
}

HRESULT CFMS30Audio::PerformFlush()
{
	CAutoLock cAutoLock(&m_csReceive);

	m_buff.Clear();
	FlushOutput(FALSE);
	FlushDecoder();

	m_rtStart = 0;
	m_bQueueResync = TRUE;

	return S_OK;
}

HRESULT CFMS30Audio::Deliver(BufferDetails &buffer)
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

