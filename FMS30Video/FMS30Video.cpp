// FMS30Video.cpp : 定义 DLL 应用程序的导出函数。
//

#include "stdafx.h"
#include "FMS30Video.h"
#include "Media.h"
#include "VideoSettings.h"
#include "IMediaSideDataFFmpeg.h"

#include "VideoInputPin.h"
#include "VideoOutputPin.h"

#include <Shlwapi.h>

#include "moreuuids.h"

#include <d3d9.h>


void CALLBACK CFMS30Video::StaticInit(BOOL bLoading, const CLSID *clsid)
{
	if (!bLoading) return;

	avcodec_register_all();
	avfilter_register_all();
}

#pragma warning(disable: 4355)

CFMS30Video::CFMS30Video(LPUNKNOWN pUnk, HRESULT* phr)
	: CTransformFilter(NAME("FMS Video Decoder"),0,__uuidof(CFMS30Video))
	,m_Decoder(this)
{
	*phr = S_OK;
	m_pInput = new CVideoInputPin(TEXT("CVideoInputPin"), this, phr, L"Input");
	ASSERT(SUCCEEDED(*phr));

	m_pOutput = new CVideoOutputPin(TEXT("CVideoOutputPin"), this, phr, L"Output");
	ASSERT(SUCCEEDED(*phr));

	memset(&m_LAVPinInfo, 0, sizeof(m_LAVPinInfo));
	memset(&m_FilterPrevFrame, 0, sizeof(m_FilterPrevFrame));
	memset(&m_SideData, 0, sizeof(m_SideData));

	StaticInit(TRUE, nullptr);

	LoadSettings();

	m_PixFmtConverter.SetSettings(this);

#ifdef DEBUG
	DbgSetModuleLevel(LOG_TRACE, DWORD_MAX);
	DbgSetModuleLevel(LOG_ERROR, DWORD_MAX);
	DbgSetModuleLevel(LOG_CUSTOM1, DWORD_MAX); // FFMPEG messages use custom1
#ifdef LAV_DEBUG_RELEASE
	DbgSetLogFileDesktop(LAVC_VIDEO_LOG_FILE);
#endif
#endif

}

CFMS30Video::~CFMS30Video()
{
	ReleaseLastSequenceFrame();
	m_Decoder.Close();

	if (m_pFilterGraph)
		avfilter_graph_free(&m_pFilterGraph);
	m_pFilterBufferSrc = nullptr;
	m_pFilterBufferSink = nullptr;


#if defined(DEBUG) && defined(LAV_DEBUG_RELEASE)
	DbgCloseLogFile();
#endif
}

// IUnknown
STDMETHODIMP CFMS30Video::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
	CheckPointer(ppv, E_POINTER);

	*ppv = nullptr;

	return
		QI2(ILAVVideoSettings)
	 __super::NonDelegatingQueryInterface(riid, ppv);
}

STDMETHODIMP CFMS30Video::SetNumThreads(DWORD dwNum)
{
	m_settings.NumThreads = dwNum;
	return S_OK;
}

STDMETHODIMP_(DWORD) CFMS30Video::GetNumThreads()
{
	return m_settings.NumThreads;
}


STDMETHODIMP CFMS30Video::SetPixelFormat(LAVOutPixFmts pixFmt, BOOL bEnabled)
{
	if (pixFmt < 0 || pixFmt >= LAVOutPixFmt_NB)
		return E_FAIL;

	m_settings.bPixFmts[pixFmt] = bEnabled;

	return S_OK;
}

STDMETHODIMP_(BOOL) CFMS30Video::GetPixelFormat(LAVOutPixFmts pixFmt)
{
	if (pixFmt < 0 || pixFmt >= LAVOutPixFmt_NB)
		return FALSE;

	return m_settings.bPixFmts[pixFmt];
}


STDMETHODIMP CFMS30Video::SetDitherMode(LAVDitherMode ditherMode)
{
	m_settings.DitherMode = ditherMode;
	return S_OK;
}

STDMETHODIMP CFMS30Video::SetSWDeintMode(LAVSWDeintModes deintMode)
{
	m_settings.SWDeintMode = deintMode;
	return S_OK;
}

STDMETHODIMP CFMS30Video::SetDeinterlacingMode(LAVDeintMode deintMode)
{
	m_settings.DeintMode = deintMode;
	return S_OK;
}

STDMETHODIMP CFMS30Video::SetSWDeintOutput(LAVDeintOutput deintOutput)
{
	m_settings.SWDeintOutput = deintOutput;
	return S_OK;
}

STDMETHODIMP CFMS30Video::Stop()
{
	// Get the receiver lock and prevent frame delivery
	{
		CAutoLock lck3(&m_csReceive);
		m_bFlushing = TRUE;
	}

	// actually perform the stop
	HRESULT hr = __super::Stop();

	// unblock delivery again, if we continue receiving frames
	m_bFlushing = FALSE;
	return hr;
}

HRESULT CFMS30Video::CheckInputType(const CMediaType* mtIn)
{
	for (UINT i = 0; i < sudPinTypesInCount; i++) {
		if (*sudPinTypesIn[i].clsMajorType == mtIn->majortype
			&& *sudPinTypesIn[i].clsMinorType == mtIn->subtype && (mtIn->formattype == FORMAT_VideoInfo || mtIn->formattype == FORMAT_VideoInfo2 || mtIn->formattype == FORMAT_MPEGVideo || mtIn->formattype == FORMAT_MPEG2Video)) {
			return S_OK;
		}
	}
	return VFW_E_TYPE_NOT_ACCEPTED;
}

// Check if the types are compatible
HRESULT CFMS30Video::CheckTransform(const CMediaType* mtIn, const CMediaType* mtOut)
{
	DbgLog((LOG_TRACE, 10, L"::CheckTransform()"));
	if (SUCCEEDED(CheckInputType(mtIn)) && mtOut->majortype == MEDIATYPE_Video) {
		if (m_PixFmtConverter.IsAllowedSubtype(&mtOut->subtype)) {
			return S_OK;
		}
	}
	return VFW_E_TYPE_NOT_ACCEPTED;
}

HRESULT CFMS30Video::DecideBufferSize(IMemAllocator * pAllocator, ALLOCATOR_PROPERTIES *pProperties)
{
	DbgLog((LOG_TRACE, 10, L"::DecideBufferSize()"));
	if (m_pInput->IsConnected() == FALSE) {
		return E_UNEXPECTED;
	}

	BITMAPINFOHEADER *pBIH = nullptr;
	CMediaType &mtOut = m_pOutput->CurrentMediaType();
	videoFormatTypeHandler(mtOut, &pBIH);

	// try to honor the requested number of downstream buffers, but cap at the decoders maximum
	long decoderBuffersMax = LONG_MAX;
	long decoderBuffs = m_Decoder.GetBufferCount(&decoderBuffersMax);
	long downstreamBuffers = pProperties->cBuffers;
	pProperties->cBuffers = min(max(pProperties->cBuffers, 2) + decoderBuffs, decoderBuffersMax);
	pProperties->cbBuffer = pBIH ? pBIH->biSizeImage : 3110400;
	pProperties->cbAlign = 1;
	pProperties->cbPrefix = 0;

	DbgLog((LOG_TRACE, 10, L" -> Downstream wants %d buffers, decoder wants %d, for a total of: %d", downstreamBuffers, decoderBuffs, pProperties->cBuffers));

	HRESULT hr;
	ALLOCATOR_PROPERTIES Actual;
	if (FAILED(hr = pAllocator->SetProperties(pProperties, &Actual))) {
		return hr;
	}

	return pProperties->cBuffers > Actual.cBuffers || pProperties->cbBuffer > Actual.cbBuffer
		? E_FAIL : S_OK;
}

HRESULT CFMS30Video::GetMediaType(int iPosition, CMediaType *pMediaType)
{
	DbgLog((LOG_TRACE, 10, L"::GetMediaType(): position: %d", iPosition));
	if (m_pInput->IsConnected() == FALSE) {
		return E_UNEXPECTED;
	}
	if (iPosition < 0) {
		return E_INVALIDARG;
	}

	if (iPosition >= (m_PixFmtConverter.GetNumMediaTypes() * 2)) {
		return VFW_S_NO_MORE_ITEMS;
	}

	int index = iPosition / 2;
	BOOL bVIH1 = iPosition % 2;

	CMediaType &mtIn = m_pInput->CurrentMediaType();

	BITMAPINFOHEADER *pBIH = nullptr;
	REFERENCE_TIME rtAvgTime = 0;
	DWORD dwAspectX = 0, dwAspectY = 0;
	videoFormatTypeHandler(mtIn.Format(), mtIn.FormatType(), &pBIH, &rtAvgTime, &dwAspectX, &dwAspectY);

	// Adjust for deinterlacing
	if (m_Decoder.IsInterlaced(FALSE) && m_settings.DeintMode != DeintMode_Disable) {
		BOOL bFramePerField = (m_settings.SWDeintMode == SWDeintMode_YADIF && m_settings.SWDeintOutput == DeintOutput_FramePerField)
			|| m_settings.SWDeintMode == SWDeintMode_W3FDIF_Simple || m_settings.SWDeintMode == SWDeintMode_W3FDIF_Complex;
		if (bFramePerField)
			rtAvgTime /= 2;
	}

	m_PixFmtConverter.GetMediaType(pMediaType, index, pBIH->biWidth, pBIH->biHeight, dwAspectX, dwAspectY, rtAvgTime, IsInterlacedOutput(), bVIH1);

	return S_OK;
}

HRESULT CFMS30Video::SetMediaType(PIN_DIRECTION dir, const CMediaType *pmt)
{
	HRESULT hr = S_OK;
	DbgLog((LOG_TRACE, 5, L"SetMediaType -- %S", dir == PINDIR_INPUT ? "in" : "out"));
	if (dir == PINDIR_INPUT) {
		hr = CreateDecoder(pmt);
		if (FAILED(hr)) {
			return hr;
		}
		m_bForceInputAR = TRUE;
	}
	else if (dir == PINDIR_OUTPUT) {
		m_PixFmtConverter.SetOutputPixFmt(m_PixFmtConverter.GetOutputBySubtype(pmt->Subtype()));
	}
	return __super::SetMediaType(dir, pmt);
}

HRESULT CFMS30Video::EndOfStream()
{
	DbgLog((LOG_TRACE, 1, L"EndOfStream, flushing decoder"));
	CAutoLock cAutoLock(&m_csReceive);

	m_Decoder.EndOfStream();
	Filter(GetFlushFrame());

	DbgLog((LOG_TRACE, 1, L"EndOfStream finished, decoder flushed"));
	return __super::EndOfStream();
}

HRESULT CFMS30Video::BeginFlush()
{
	DbgLog((LOG_TRACE, 1, L"::BeginFlush"));
	m_bFlushing = TRUE;
	return __super::BeginFlush();
}

HRESULT CFMS30Video::EndFlush()
{
	DbgLog((LOG_TRACE, 1, L"::EndFlush"));
	CAutoLock cAutoLock(&m_csReceive);

	ReleaseLastSequenceFrame();

	if (m_dwDecodeFlags & LAV_VIDEO_DEC_FLAG_DVD) {
		PerformFlush();
	}

	HRESULT hr = __super::EndFlush();
	m_bFlushing = FALSE;
	return hr;
}

HRESULT CFMS30Video::NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate)
{
	DbgLog((LOG_TRACE, 1, L"::NewSegment - %I64d / %I64d", tStart, tStop));

	PerformFlush();

	return __super::NewSegment(tStart, tStop, dRate);
}

HRESULT CFMS30Video::Receive(IMediaSample *pIn)
{
	CAutoLock cAutoLock(&m_csReceive);
	HRESULT        hr = S_OK;

	AM_SAMPLE2_PROPERTIES const *pProps = m_pInput->SampleProps();
	if (pProps->dwStreamId != AM_STREAM_MEDIA) {
		return m_pOutput->Deliver(pIn);
	}

	AM_MEDIA_TYPE *pmt = nullptr;
	if (SUCCEEDED(pIn->GetMediaType(&pmt)) && pmt) {
		CMediaType mt = *pmt;
		DeleteMediaType(pmt);
		if (mt != m_pInput->CurrentMediaType() || !(m_dwDecodeFlags & LAV_VIDEO_DEC_FLAG_DVD)) {
			DbgLog((LOG_TRACE, 10, L"::Receive(): Input sample contained media type, dynamic format change..."));
			m_Decoder.EndOfStream();
			hr = m_pInput->SetMediaType(&mt);
			if (FAILED(hr)) {
				DbgLog((LOG_ERROR, 10, L"::Receive(): Setting new media type failed..."));
				return hr;
			}
		}
	}

	m_hrDeliver = S_OK;

	// Skip over empty packets
	if (pIn->GetActualDataLength() == 0) {
		return S_OK;
	}

	hr = m_Decoder.Decode(pIn);
	if (FAILED(hr))
		return hr;

	if (FAILED(m_hrDeliver))
		return m_hrDeliver;

	return S_OK;

}

HRESULT CFMS30Video::CheckConnect(PIN_DIRECTION dir, IPin *pPin)
{
	//wxg mark  这些guid都需要修改一下
	if (dir == PINDIR_INPUT) {
		if (FilterInGraphSafe(pPin, CLSID_LAVVideo, TRUE)) {
			DbgLog((LOG_TRACE, 10, L"CFMS30Video::CheckConnect(): CFMS30Video is already in this graph branch, aborting."));
			return E_FAIL;
		}
	}
	else if (dir == PINDIR_OUTPUT) {
		// Get the filter we're connecting to
		IBaseFilter *pFilter = GetFilterFromPin(pPin);
		CLSID guidFilter = GUID_NULL;
		if (pFilter != nullptr) {
			if (FAILED(pFilter->GetClassID(&guidFilter)))
				guidFilter = GUID_NULL;

			SafeRelease(&pFilter);
		}

		// Don't allow connection to the AVI Decompressor, it doesn't support a variety of our output formats properly
		if (guidFilter == CLSID_AVIDec)
			return VFW_E_TYPE_NOT_ACCEPTED;
	}
	return __super::CheckConnect(dir, pPin);
}

HRESULT CFMS30Video::BreakConnect(PIN_DIRECTION dir)
{
	DbgLog((LOG_TRACE, 10, L"::BreakConnect"));
	if (dir == PINDIR_INPUT) {
		if (m_pFilterGraph)
			avfilter_graph_free(&m_pFilterGraph);

		m_Decoder.Close();
	}
	else if (dir == PINDIR_OUTPUT)
	{
		m_Decoder.BreakConnect();
	}
	return __super::BreakConnect(dir);
}

HRESULT CFMS30Video::CompleteConnect(PIN_DIRECTION dir, IPin *pReceivePin)
{
	DbgLog((LOG_TRACE, 10, L"::CompleteConnect"));
	HRESULT hr = S_OK;
	if (dir == PINDIR_OUTPUT) {
		BOOL bFailNonDXVA = false;
		// Fail P010 software connections before Windows 10 Creators Update (presumably it was fixed before Creators already, but this is definitely a safe known condition)
		if (!IsWindows10BuildOrNewer(15063) && (m_pOutput->CurrentMediaType().subtype == MEDIASUBTYPE_P010 || m_pOutput->CurrentMediaType().subtype == MEDIASUBTYPE_P016)) {
			// Check if we're connecting to EVR
			IBaseFilter *pFilter = GetFilterFromPin(pReceivePin);
			if (pFilter != nullptr) {
				CLSID guid;
				if (SUCCEEDED(pFilter->GetClassID(&guid))) {
					DbgLog((LOG_TRACE, 10, L"-> Connecting P010/P016 to %s", WStringFromGUID(guid).c_str()));
					bFailNonDXVA = (guid == CLSID_EnhancedVideoRenderer || guid == CLSID_VideoMixingRenderer9 || guid == CLSID_VideoMixingRenderer);
				}
				SafeRelease(&pFilter);
			}
		}

		hr = m_Decoder.PostConnect(pReceivePin);
		if (SUCCEEDED(hr)) {
			if (bFailNonDXVA) {
				LAVPixelFormat fmt;
				m_Decoder.GetPixelFormat(&fmt, nullptr);

				if (fmt != LAVPixFmt_DXVA2) {
					DbgLog((LOG_TRACE, 10, L"-> Non-DXVA2 Connection rejected on this renderer and subtype"));
					return VFW_E_TYPE_NOT_ACCEPTED;
				}
			}

			CheckDirectMode();
		}
	}
	else if (dir == PINDIR_INPUT) {

	}
	return hr;
}

int CFMS30Video::GetPinCount()
{
	return 2;
}

CBasePin* CFMS30Video::GetPin(int n)
{
	return __super::GetPin(n);
}

//IBaseFilter::JoinFilterGraph method,we have do nothing
STDMETHODIMP CFMS30Video::JoinFilterGraph(IFilterGraph * pGraph, LPCWSTR pName)
{
	CAutoLock cObjectLock(m_pLock);
	HRESULT hr = __super::JoinFilterGraph(pGraph, pName);
	return hr;
}

HRESULT CFMS30Video::EndOfSegment()
{
	DbgLog((LOG_TRACE, 1, L"EndOfSegment, flushing decoder"));
	CAutoLock cAutoLock(&m_csReceive);

	m_Decoder.EndOfStream();
	Filter(GetFlushFrame());

	// Forward the EndOfSegment call downstream
	if (m_pOutput != NULL && m_pOutput->IsConnected())
	{
		IPin *pConnected = m_pOutput->GetConnected();

		IPinSegmentEx *pPinSegmentEx = NULL;
		if (pConnected->QueryInterface(&pPinSegmentEx) == S_OK)
		{
			pPinSegmentEx->EndOfSegment();
		}
		SafeRelease(&pPinSegmentEx);
	}

	return S_OK;
}

STDMETHODIMP CFMS30Video::AllocateFrame(LAVFrame **ppFrame)
{
	CheckPointer(ppFrame, E_POINTER);

	*ppFrame = (LAVFrame *)CoTaskMemAlloc(sizeof(LAVFrame));
	if (!*ppFrame) {
		return E_OUTOFMEMORY;
	}

	// Initialize with zero
	ZeroMemory(*ppFrame, sizeof(LAVFrame));

	// Set some defaults
	(*ppFrame)->bpp = 8;
	(*ppFrame)->rtStart = AV_NOPTS_VALUE;
	(*ppFrame)->rtStop = AV_NOPTS_VALUE;
	(*ppFrame)->aspect_ratio = { 0, 1 };

	(*ppFrame)->frame_type = '?';

	return S_OK;
}

STDMETHODIMP CFMS30Video::ReleaseFrame(LAVFrame **ppFrame)
{
	CheckPointer(ppFrame, E_POINTER);

	// Allow *ppFrame to be NULL already
	if (*ppFrame) {
		FreeLAVFrameBuffers(*ppFrame);
		SAFE_CO_FREE(*ppFrame);
	}
	return S_OK;
}

STDMETHODIMP CFMS30Video::Deliver(LAVFrame *pFrame)
{
	// Out-of-sequence flush event to get all frames delivered,
	// only triggered by decoders when they are already "empty"
	// so no need to flush the decoder here
	if (pFrame->flags & LAV_FRAME_FLAG_FLUSH) {
		DbgLog((LOG_TRACE, 10, L"Decoder triggered a flush..."));
		Filter(GetFlushFrame());

		ReleaseFrame(&pFrame);
		return S_FALSE;
	}

	if (m_bFlushing) {
		ReleaseFrame(&pFrame);
		return S_FALSE;
	}

	if (pFrame->rtStart == AV_NOPTS_VALUE) {
		pFrame->rtStart = m_rtPrevStop;
		pFrame->rtStop = AV_NOPTS_VALUE;
	}

	if (pFrame->rtStop == AV_NOPTS_VALUE) {
		REFERENCE_TIME duration = 0;

		CMediaType &mt = m_pOutput->CurrentMediaType();
		videoFormatTypeHandler(mt.Format(), mt.FormatType(), nullptr, &duration, nullptr, nullptr);

		REFERENCE_TIME decoderDuration = m_Decoder.GetFrameDuration();
		if (pFrame->avgFrameDuration && pFrame->avgFrameDuration != AV_NOPTS_VALUE) {
			duration = pFrame->avgFrameDuration;
		}
		else if (!duration && decoderDuration) {
			duration = decoderDuration;
		}
		else if (!duration) {
			duration = 1;
		}

		pFrame->rtStop = pFrame->rtStart + (duration * (pFrame->repeat ? 3 : 2) / 2);
	}

#if defined(DEBUG) && DEBUG_FRAME_TIMINGS
	DbgLog((LOG_TRACE, 10, L"Frame, rtStart: %I64d, dur: %I64d, diff: %I64d, key: %d, type: %C, repeat: %d, interlaced: %d, tff: %d", pFrame->rtStart, pFrame->rtStop - pFrame->rtStart, pFrame->rtStart - m_rtPrevStart, pFrame->key_frame, pFrame->frame_type, pFrame->repeat, pFrame->interlaced, pFrame->tff));
#endif

	m_rtPrevStart = pFrame->rtStart;
	m_rtPrevStop = pFrame->rtStop;

	if (!pFrame->avgFrameDuration || pFrame->avgFrameDuration == AV_NOPTS_VALUE)
		pFrame->avgFrameDuration = m_rtAvgTimePerFrame;
	else
		m_rtAvgTimePerFrame = pFrame->avgFrameDuration;

	if (pFrame->rtStart < 0) {
		ReleaseFrame(&pFrame);
		return S_OK;
	}

	if (!(m_Decoder.IsInterlaced(FALSE) && m_settings.SWDeintMode != SWDeintMode_None)
		|| pFrame->flags & LAV_FRAME_FLAG_REDRAW) {
		return DeliverToRenderer(pFrame);
	}
	else {
		Filter(pFrame);
	}
	return S_OK;


}

STDMETHODIMP_(LAVSWDeintModes) CFMS30Video::GetSWDeintMode()
{
	return (LAVSWDeintModes)m_settings.SWDeintMode;
}

STDMETHODIMP_(LAVDeintMode) CFMS30Video::GetDeinterlacingMode()
{
	return m_settings.DeintMode;
}

STDMETHODIMP_(LAVDeintOutput) CFMS30Video::GetSWDeintOutput()
{
	return (LAVDeintOutput)m_settings.SWDeintOutput;
}

STDMETHODIMP_(LAVDitherMode) CFMS30Video::GetDitherMode()
{
	return (LAVDitherMode)m_settings.DitherMode;
}

STDMETHODIMP_(BOOL) CFMS30Video::HasDynamicInputAllocator()
{
	return dynamic_cast<CVideoInputPin*>(m_pInput)->HasDynamicAllocator();
}

STDMETHODIMP_(LAVFrame*) CFMS30Video::GetFlushFrame()
{
	LAVFrame *pFlushFrame = nullptr;
	AllocateFrame(&pFlushFrame);
	pFlushFrame->flags |= LAV_FRAME_FLAG_FLUSH;
	pFlushFrame->rtStart = INT64_MAX;
	pFlushFrame->rtStop = INT64_MAX;
	return pFlushFrame;
}

STDMETHODIMP_(LPWSTR) CFMS30Video::GetFileExtension()
{
	if (m_strExtension.empty()) {
		m_strExtension = L"";

		IFileSourceFilter *pSource = nullptr;
		if (SUCCEEDED(FindIntefaceInGraph(m_pInput, IID_IFileSourceFilter, (void **)&pSource))) {
			LPOLESTR pwszFile = nullptr;
			if (SUCCEEDED(pSource->GetCurFile(&pwszFile, nullptr)) && pwszFile) {
				LPWSTR pwszExtension = PathFindExtensionW(pwszFile);
				m_strExtension = std::wstring(pwszExtension);
				CoTaskMemFree(pwszFile);
			}
			SafeRelease(&pSource);
		}
	}

	size_t len = m_strExtension.size() + 1;
	LPWSTR pszExtension = (LPWSTR)CoTaskMemAlloc(sizeof(WCHAR) * len);
	if (!pszExtension)
		return nullptr;

	wcscpy_s(pszExtension, len, m_strExtension.c_str());
	return pszExtension;
}

static const LPWSTR stream_ar_blacklist[] = {
	L".mkv", L".webm",
	L".mp4", L".mov", L".m4v",
};

HRESULT CFMS30Video::LoadDefaults()
{
	m_settings.StreamAR = 2;
	m_settings.NumThreads = 0;
	m_settings.DeintFieldOrder = DeintFieldOrder_Auto;
	m_settings.DeintMode = DeintMode_Auto;
	m_settings.RGBRange = 2;
	for (int i = 0; i < Codec_VideoNB; ++i)
		m_settings.bFormats[i] = TRUE;

	m_settings.bFormats[Codec_RV12] = FALSE;
	m_settings.bFormats[Codec_QPEG] = FALSE;
	m_settings.bFormats[Codec_MSRLE] = FALSE;
	m_settings.bFormats[Codec_CineformHD] = FALSE;
	m_settings.bFormats[Codec_v210] = FALSE;

	for (int i = 0; i < LAVOutPixFmt_NB; ++i)
		m_settings.bPixFmts[i] = TRUE;

	m_settings.bPixFmts[LAVOutPixFmt_YV16] = FALSE;
	m_settings.bPixFmts[LAVOutPixFmt_AYUV] = FALSE;
	m_settings.bPixFmts[LAVOutPixFmt_RGB32] = TRUE;	

	m_settings.SWDeintMode = SWDeintMode_None;
	m_settings.SWDeintOutput = DeintOutput_FramePerField;

	m_settings.DitherMode = LAVDither_Random;
	
	return S_OK;
}

HRESULT CFMS30Video::LoadSettings()
{
	LoadDefaults();
	if (m_bRuntimeConfig)
		return S_FALSE;

	return S_OK;
}

HRESULT CFMS30Video::CreateDecoder(const CMediaType *pmt)
{
	DbgLog((LOG_TRACE, 10, L"::CreateDecoder(): Creating new decoder..."));
	HRESULT hr = S_OK;

	AVCodecID codec = FindCodecId(pmt);
	if (codec == AV_CODEC_ID_NONE) {
		return VFW_E_TYPE_NOT_ACCEPTED;
	}

	// Check if codec is activated
	for (int i = 0; i < Codec_VideoNB; ++i) {
		const codec_config_t *config = get_codec_config((LAVVideoCodec)i);
		bool bMatched = false;
		for (int k = 0; k < config->nCodecs; ++k) {
			if (config->codecs[k] == codec) {
				bMatched = true;
				break;
			}
		}
		if (bMatched && !m_settings.bFormats[i]) {
			DbgLog((LOG_TRACE, 10, L"-> Codec is disabled"));
			return VFW_E_TYPE_NOT_ACCEPTED;
		}
	}

	ILAVPinInfo *pPinInfo = nullptr;
	hr = FindPinIntefaceInGraph(m_pInput, IID_ILAVPinInfo, (void **)&pPinInfo);
	if (SUCCEEDED(hr)) {
		memset(&m_LAVPinInfo, 0, sizeof(m_LAVPinInfo));

		m_LAVPinInfoValid = TRUE;
		m_LAVPinInfo.flags = pPinInfo->GetStreamFlags();
		m_LAVPinInfo.pix_fmt = (AVPixelFormat)pPinInfo->GetPixelFormat();
		m_LAVPinInfo.has_b_frames = pPinInfo->GetHasBFrames();

		SafeRelease(&pPinInfo);
	}
	else {
		m_LAVPinInfoValid = FALSE;
	}

	// Clear old sidedata
	memset(&m_SideData, 0, sizeof(m_SideData));

	// Read and store stream-level sidedata
	IMediaSideData *pPinSideData = nullptr;
	hr = FindPinIntefaceInGraph(m_pInput, __uuidof(IMediaSideData), (void **)&pPinSideData);
	if (SUCCEEDED(hr)) {
		MediaSideDataFFMpeg *pSideData = nullptr;
		size_t size = 0;
		hr = pPinSideData->GetSideData(IID_MediaSideDataFFMpeg, (const BYTE **)&pSideData, &size);
		if (SUCCEEDED(hr) && size == sizeof(MediaSideDataFFMpeg)) {
			for (int i = 0; i < pSideData->side_data_elems; i++) {
				AVPacketSideData *sd = &pSideData->side_data[i];

				// Display Mastering metadata, including color info
				if (sd->type == AV_PKT_DATA_MASTERING_DISPLAY_METADATA && sd->size == sizeof(AVMasteringDisplayMetadata))
				{
					m_SideData.Mastering = *(AVMasteringDisplayMetadata *)sd->data;
				}
			}
		}

		SafeRelease(&pPinSideData);
	}

	m_dwDecodeFlags = 0;

	LPWSTR pszExtension = GetFileExtension();
	if (pszExtension) {
		DbgLog((LOG_TRACE, 10, L"-> File extension: %s", pszExtension));

		for (int i = 0; i < countof(stream_ar_blacklist); i++) {
			if (_wcsicmp(stream_ar_blacklist[i], pszExtension) == 0) {
				m_dwDecodeFlags |= LAV_VIDEO_DEC_FLAG_STREAMAR_BLACKLIST;
				break;
			}
		}
		if (m_dwDecodeFlags & LAV_VIDEO_DEC_FLAG_STREAMAR_BLACKLIST) {
			// MPC-HC MP4 Splitter fails at Container AR
			if (FilterInGraph(PINDIR_INPUT, CLSID_MPCHCMP4Splitter) || FilterInGraph(PINDIR_INPUT, CLSID_MPCHCMP4SplitterSource)) {
				m_dwDecodeFlags &= ~LAV_VIDEO_DEC_FLAG_STREAMAR_BLACKLIST;
			}
		}
	}

	SAFE_CO_FREE(pszExtension);

	hr = m_Decoder.CreateDecoder(pmt, codec);
	if (FAILED(hr)) {
		DbgLog((LOG_TRACE, 10, L"-> Decoder creation failed"));
		goto done;
	}
	// Get avg time per frame
	videoFormatTypeHandler(pmt->Format(), pmt->FormatType(), nullptr, &m_rtAvgTimePerFrame);

	LAVPixelFormat pix;
	int bpp;
	m_Decoder.GetPixelFormat(&pix, &bpp);

	// Set input on the converter, and force negotiation if needed
	if (m_PixFmtConverter.SetInputFmt(pix, bpp) && m_pOutput->IsConnected())
		m_bForceFormatNegotiation = TRUE;

	if (pix == LAVPixFmt_YUV420 || pix == LAVPixFmt_YUV422 || pix == LAVPixFmt_NV12)
		m_filterPixFmt = pix;

done:
	return SUCCEEDED(hr) ? S_OK : VFW_E_TYPE_NOT_ACCEPTED;
}

HRESULT CFMS30Video::GetDeliveryBuffer(IMediaSample** ppOut, int width, int height, AVRational ar, DXVA2_ExtendedFormat dxvaExtFlags, REFERENCE_TIME avgFrameDuration)
{
	CheckPointer(ppOut, E_POINTER);

	HRESULT hr;

	if (FAILED(hr = ReconnectOutput(width, height, ar, dxvaExtFlags, avgFrameDuration))) {
		return hr;
	}

	if (FAILED(hr = m_pOutput->GetDeliveryBuffer(ppOut, nullptr, nullptr, 0))) {
		return hr;
	}

	CheckPointer(*ppOut, E_UNEXPECTED);

	AM_MEDIA_TYPE* pmt = nullptr;
	if (SUCCEEDED((*ppOut)->GetMediaType(&pmt)) && pmt) {
		//wxg20180223如果走到这里实际上就是失败，只是便于调试问题
		CMediaType &outMt = m_pOutput->CurrentMediaType();
		BITMAPINFOHEADER *pBMINew = nullptr;
		videoFormatTypeHandler(pmt->pbFormat, &pmt->formattype, &pBMINew);

		if (pBMINew->biWidth < width) {
			DbgLog((LOG_TRACE, 10, L" -> Renderer is trying to shrink the output window, failing!"));
			(*ppOut)->Release();
			(*ppOut) = nullptr;
			DeleteMediaType(pmt);
			return E_FAIL;
		}
		CMediaType mt = *pmt;
		m_pOutput->SetMediaType(&mt);
		DeleteMediaType(pmt);
		pmt = nullptr;
	}

	(*ppOut)->SetDiscontinuity(FALSE);
	(*ppOut)->SetSyncPoint(TRUE);

	return S_OK;
}

HRESULT CFMS30Video::ReconnectOutput(int width, int height, AVRational ar, DXVA2_ExtendedFormat dxvaExtFlags, REFERENCE_TIME avgFrameDuration, BOOL bDXVA /*= FALSE*/)
{
	CMediaType mt = m_pOutput->CurrentMediaType();

	HRESULT hr = S_FALSE;
	BOOL bNeedReconnect = FALSE;
	int timeout = 100;

	DWORD dwAspectX = 0, dwAspectY = 0;
	RECT rcTargetOld = { 0 };
	LONG biWidthOld = 0;

	// HACK: 1280 is the value when only chroma location is set to MPEG2, do not bother to send this information, as its the same for basically every clip.
	if ((dxvaExtFlags.value & ~0xff) != 0 && (dxvaExtFlags.value & ~0xff) != 1280)
		dxvaExtFlags.SampleFormat = AMCONTROL_USED | AMCONTROL_COLORINFO_PRESENT;
	else
		dxvaExtFlags.value = 0;

	BOOL bInterlaced = IsInterlacedOutput();
	DWORD dwInterlacedFlags = 0;
	dwInterlacedFlags = bInterlaced ? AMINTERLACE_IsInterlaced | AMINTERLACE_DisplayModeBobOrWeave : 0;

	//wxg不进行重新连接，只是保留这些函数

	return S_FALSE;
}

HRESULT CFMS30Video::SetFrameFlags(IMediaSample* pMS, LAVFrame *pFrame)
{
	HRESULT hr = S_OK;
	IMediaSample2 *pMS2 = nullptr;
	if (SUCCEEDED(hr = pMS->QueryInterface(&pMS2))) {
		AM_SAMPLE2_PROPERTIES props;
		if (SUCCEEDED(pMS2->GetProperties(sizeof(props), (BYTE*)&props))) {
			props.dwTypeSpecificFlags &= ~0x7f;

			if (!pFrame->interlaced)
				props.dwTypeSpecificFlags |= AM_VIDEO_FLAG_WEAVE;

			if (pFrame->tff)
				props.dwTypeSpecificFlags |= AM_VIDEO_FLAG_FIELD1FIRST;

			if (pFrame->repeat)
				props.dwTypeSpecificFlags |= AM_VIDEO_FLAG_REPEAT_FIELD;

			pMS2->SetProperties(sizeof(props), (BYTE*)&props);
		}
	}
	SafeRelease(&pMS2);
	return hr;
}

HRESULT CFMS30Video::NegotiatePixelFormat(CMediaType &outMt, int width, int height)
{
	DbgLog((LOG_TRACE, 10, L"::NegotiatePixelFormat()"));

	HRESULT hr = S_OK;
	int i = 0;
	int timeout = 100;

	DWORD dwAspectX, dwAspectY;
	REFERENCE_TIME rtAvg;
	BOOL bVIH1 = (outMt.formattype == FORMAT_VideoInfo);
	videoFormatTypeHandler(outMt.Format(), outMt.FormatType(), nullptr, &rtAvg, &dwAspectX, &dwAspectY);

	CMediaType mt;
	for (i = 0; i < m_PixFmtConverter.GetNumMediaTypes(); ++i) {
		m_PixFmtConverter.GetMediaType(&mt, i, width, height, dwAspectX, dwAspectY, rtAvg, IsInterlacedOutput(), bVIH1);
		//hr = m_pOutput->GetConnected()->QueryAccept(&mt);
	receiveconnection:
		hr = m_pOutput->GetConnected()->ReceiveConnection(m_pOutput, &mt);
		if (hr == S_OK) {
			DbgLog((LOG_TRACE, 10, L"::NegotiatePixelFormat(): Filter accepted format with index %d", i));
			m_pOutput->SetMediaType(&mt);
			hr = S_OK;
			goto done;
		}
		else if (hr == VFW_E_BUFFERS_OUTSTANDING && timeout != -1) {
			if (timeout > 0) {
				DbgLog((LOG_TRACE, 10, L"-> Buffers outstanding, retrying in 10ms.."));
				Sleep(10);
				timeout -= 10;
			}
			else {
				DbgLog((LOG_TRACE, 10, L"-> Buffers outstanding, timeout reached, flushing.."));
				m_pOutput->DeliverBeginFlush();
				m_pOutput->DeliverEndFlush();
				timeout = -1;
			}
			goto receiveconnection;
		}
	}

	DbgLog((LOG_ERROR, 10, L"::NegotiatePixelFormat(): Unable to agree on a pixel format"));
	hr = E_FAIL;

done:
	FreeMediaType(mt);
	return hr;
}

BOOL CFMS30Video::IsInterlacedOutput()
{
	return (m_settings.SWDeintMode == SWDeintMode_None || m_filterPixFmt == LAVPixFmt_None) && m_Decoder.IsInterlaced(TRUE) && !(m_settings.DeintMode == DeintMode_Disable);
}

HRESULT CFMS30Video::CheckDirectMode()
{
	LAVPixelFormat pix;
	int bpp;
	m_Decoder.GetPixelFormat(&pix, &bpp);

	BOOL bDirect = (pix == LAVPixFmt_NV12 || pix == LAVPixFmt_P016);
	if (pix == LAVPixFmt_NV12 && m_Decoder.IsInterlaced(FALSE) && m_settings.SWDeintMode != SWDeintMode_None)
		bDirect = FALSE;
	else if (pix == LAVPixFmt_NV12 && m_pOutput->CurrentMediaType().subtype != MEDIASUBTYPE_NV12 && m_pOutput->CurrentMediaType().subtype != MEDIASUBTYPE_YV12)
		bDirect = FALSE;
	else if (pix == LAVPixFmt_P016 && m_pOutput->CurrentMediaType().subtype != MEDIASUBTYPE_P010 && m_pOutput->CurrentMediaType().subtype != MEDIASUBTYPE_P016 && m_pOutput->CurrentMediaType().subtype != MEDIASUBTYPE_NV12)
		bDirect = FALSE;

	m_Decoder.SetDirectOutput(bDirect);

	return S_OK;
}

HRESULT CFMS30Video::DeDirectFrame(LAVFrame *pFrame, bool bDisableDirectMode /*= true*/)
{
	if (!pFrame->direct)
		return S_FALSE;

	ASSERT(pFrame->direct_lock && pFrame->direct_unlock);

	LAVPixFmtDesc desc = getPixelFormatDesc(pFrame->format);

	LAVFrame tmpFrame = *pFrame;
	pFrame->destruct = nullptr;
	pFrame->priv_data = nullptr;
	pFrame->direct = false;
	pFrame->direct_lock = nullptr;
	pFrame->direct_unlock = nullptr;
	memset(pFrame->data, 0, sizeof(pFrame->data));

	// sidedata remains on the main frame
	tmpFrame.side_data = nullptr;
	tmpFrame.side_data_count = 0;

	LAVDirectBuffer buffer;
	if (tmpFrame.direct_lock(&tmpFrame, &buffer)) {
		HRESULT hr = AllocLAVFrameBuffers(pFrame, buffer.stride[0] / desc.codedbytes);
		if (FAILED(hr)) {
			tmpFrame.direct_unlock(&tmpFrame);
			FreeLAVFrameBuffers(&tmpFrame);
			return hr;
		}

		// use slow copy, this should only be used extremely rarely
		memcpy(pFrame->data[0], buffer.data[0], pFrame->height * buffer.stride[0]);
		for (int i = 1; i < desc.planes; i++)
			memcpy(pFrame->data[i], buffer.data[i], (pFrame->height / desc.planeHeight[i]) * buffer.stride[i]);

		tmpFrame.direct_unlock(&tmpFrame);
	} else {
		// fallack, alloc anyway so nothing blows up
		HRESULT hr = AllocLAVFrameBuffers(pFrame);
		if (FAILED(hr)) {
			FreeLAVFrameBuffers(&tmpFrame);
			return hr;
		}
	}

	FreeLAVFrameBuffers(&tmpFrame);

	if (bDisableDirectMode)
		m_Decoder.SetDirectOutput(false);

	return S_OK;
}

HRESULT CFMS30Video::DeliverToRenderer(LAVFrame *pFrame)
{
	HRESULT hr = S_OK;

	// This should never get here, but better check
	if (pFrame->flags & LAV_FRAME_FLAG_FLUSH) {
		ReleaseFrame(&pFrame);
		return S_FALSE;
	}

	if (!(pFrame->flags & LAV_FRAME_FLAG_REDRAW)) {
		// Release the old End-of-Sequence frame, this ensures any "normal" frame will clear the stored EOS frame
		if (pFrame->format != LAVPixFmt_DXVA2 && pFrame->format != LAVPixFmt_D3D11) {
			ReleaseFrame(&m_pLastSequenceFrame);
			if ((pFrame->flags & LAV_FRAME_FLAG_END_OF_SEQUENCE)) {
				if (pFrame->direct) {
					hr = DeDirectFrame(pFrame, false);
					if (FAILED(hr)) {
						ReleaseFrame(&pFrame);
						return hr;
					}
				}
				CopyLAVFrame(pFrame, &m_pLastSequenceFrame);
			}
		}
		else if (pFrame->format == LAVPixFmt_DXVA2) {
			// TODO DXVA2
		}
		else if (pFrame->format == LAVPixFmt_D3D11)
		{
			// TODO D3D11
		}
	}

	if (m_bFlushing) {
		ReleaseFrame(&pFrame);
		return S_FALSE;
	}

	// Process stream-level sidedata and attach it to the frame if necessary
	if (m_SideData.Mastering.has_luminance || m_SideData.Mastering.has_primaries) {
		MediaSideDataHDR *hdr = nullptr;

		// Check if HDR data already exists
		if (pFrame->side_data && pFrame->side_data_count) {
			for (int i = 0; i < pFrame->side_data_count; i++) {
				if (pFrame->side_data[i].guidType == IID_MediaSideDataHDR) {
					hdr = (MediaSideDataHDR *)pFrame->side_data[i].data;
					break;
				}
			}
		}

		if (hdr == nullptr)
			hdr = (MediaSideDataHDR *)AddLAVFrameSideData(pFrame, IID_MediaSideDataHDR, sizeof(MediaSideDataHDR));

		processFFHDRData(hdr, &m_SideData.Mastering);
	}

	// Collect width/height
	int width = pFrame->width;
	int height = pFrame->height;

	if (width == 1920 && height == 1088) {
		height = 1080;
	}

	if (m_PixFmtConverter.SetInputFmt(pFrame->format, pFrame->bpp) || m_bForceFormatNegotiation) {
		DbgLog((LOG_TRACE, 10, L"::Decode(): Changed input pixel format to %d (%d bpp)", pFrame->format, pFrame->bpp));

		CMediaType& mt = m_pOutput->CurrentMediaType();

		if (m_PixFmtConverter.GetOutputBySubtype(mt.Subtype()) != m_PixFmtConverter.GetPreferredOutput()) {
			NegotiatePixelFormat(mt, width, height);
		}
		m_bForceFormatNegotiation = FALSE;
	}
	m_PixFmtConverter.SetColorProps(pFrame->ext_format, m_settings.RGBRange);

	// Update flags for cases where the converter can change the nominal range
	if (m_PixFmtConverter.IsRGBConverterActive()) {
		if (m_settings.RGBRange != 0)
			pFrame->ext_format.NominalRange = m_settings.RGBRange == 1 ? DXVA2_NominalRange_16_235 : DXVA2_NominalRange_0_255;
		else if (pFrame->ext_format.NominalRange == DXVA2_NominalRange_Unknown)
			pFrame->ext_format.NominalRange = DXVA2_NominalRange_16_235;
	}
	else if (m_PixFmtConverter.GetOutputPixFmt() == LAVOutPixFmt_RGB32 || m_PixFmtConverter.GetOutputPixFmt() == LAVOutPixFmt_RGB24 || m_PixFmtConverter.GetOutputPixFmt() == LAVOutPixFmt_RGB48) {
		pFrame->ext_format.NominalRange = DXVA2_NominalRange_0_255;
	}

	// Check if we are doing RGB output
	BOOL bRGBOut = (m_PixFmtConverter.GetOutputPixFmt() == LAVOutPixFmt_RGB24 || m_PixFmtConverter.GetOutputPixFmt() == LAVOutPixFmt_RGB32);
	// And blend subtitles if we're on YUV output before blending (because the output YUV formats are more complicated to handle)


	// Grab a media sample, and start assembling the data for it.
	IMediaSample *pSampleOut = nullptr;
	BYTE         *pDataOut = nullptr;

	REFERENCE_TIME avgDuration = pFrame->avgFrameDuration;
	if (avgDuration == 0)
		avgDuration = AV_NOPTS_VALUE;

	if (FAILED(hr = GetDeliveryBuffer(&pSampleOut, width, height, pFrame->aspect_ratio, pFrame->ext_format, avgDuration)) || FAILED(hr = pSampleOut->GetPointer(&pDataOut)) || pDataOut == nullptr) {
		SafeRelease(&pSampleOut);
		ReleaseFrame(&pFrame);
		return hr;
	}

	CMediaType& mt = m_pOutput->CurrentMediaType();
	BITMAPINFOHEADER *pBIH = nullptr;
	videoFormatTypeHandler(mt.Format(), mt.FormatType(), &pBIH);

	// Set side data on the media sample
	if (pFrame->side_data_count) {
		IMediaSideData *pMediaSideData = nullptr;
		if (SUCCEEDED(hr = pSampleOut->QueryInterface(&pMediaSideData))) {
			for (int i = 0; i < pFrame->side_data_count; i++)
				pMediaSideData->SetSideData(pFrame->side_data[i].guidType, pFrame->side_data[i].data, pFrame->side_data[i].size);

			SafeRelease(&pMediaSideData);
		}
	}

	if (pFrame->format != LAVPixFmt_DXVA2 && pFrame->format != LAVPixFmt_D3D11) {
		long required = m_PixFmtConverter.GetImageSize(pBIH->biWidth, abs(pBIH->biHeight));

		long lSampleSize = pSampleOut->GetSize();
		if (lSampleSize < required) {
			DbgLog((LOG_ERROR, 10, L"::Decode(): Buffer is too small! Actual: %d, Required: %d", lSampleSize, required));
			SafeRelease(&pSampleOut);
			ReleaseFrame(&pFrame);
			return E_FAIL;
		}
		pSampleOut->SetActualDataLength(required);

#if defined(DEBUG) && DEBUG_PIXELCONV_TIMINGS
		LARGE_INTEGER frequency, start, end;
		QueryPerformanceFrequency(&frequency);
		QueryPerformanceCounter(&start);
#endif

		if (pFrame->direct && !m_PixFmtConverter.IsDirectModeSupported((uintptr_t)pDataOut, pBIH->biWidth)) {
			DeDirectFrame(pFrame, true);
		}

		if (pFrame->direct)
			m_PixFmtConverter.ConvertDirect(pFrame, pDataOut, width, height, pBIH->biWidth, abs(pBIH->biHeight));
		else
			m_PixFmtConverter.Convert(pFrame->data, pFrame->stride, pDataOut, width, height, pBIH->biWidth, abs(pBIH->biHeight));

#if defined(DEBUG) && DEBUG_PIXELCONV_TIMINGS
		QueryPerformanceCounter(&end);
		double diff = (end.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart;
		m_pixFmtTimingAvg.Sample(diff);

		DbgLog((LOG_TRACE, 10, L"Pixel Mapping took %2.3fms in avg", m_pixFmtTimingAvg.Average()));
#endif

		// Once we're done with the old frame, release its buffers
		// This does not release the frame yet, just free its buffers
		FreeLAVFrameBuffers(pFrame);

		// .. and if we do RGB conversion, blend after the conversion, for improved quality

		if ((mt.subtype == MEDIASUBTYPE_RGB32 || mt.subtype == MEDIASUBTYPE_RGB24) && pBIH->biHeight > 0) {
			int bpp = (mt.subtype == MEDIASUBTYPE_RGB32) ? 4 : 3;
			flip_plane(pDataOut, pBIH->biWidth * bpp, height);
		}
	}

	BOOL bSizeChanged = FALSE;

	// Set frame timings..
	pSampleOut->SetTime(&pFrame->rtStart, &pFrame->rtStop);
	pSampleOut->SetMediaTime(nullptr, nullptr);

	// And frame flags..
	SetFrameFlags(pSampleOut, pFrame);

	// Release frame before delivery, so it can be re-used by the decoder (if required)
	ReleaseFrame(&pFrame);

	hr = m_pOutput->Deliver(pSampleOut);
	if (FAILED(hr)) {
		DbgLog((LOG_ERROR, 10, L"::Decode(): Deliver failed with hr: %x", hr));
		m_hrDeliver = hr;
	}

	if (bSizeChanged)
		NotifyEvent(EC_VIDEO_SIZE_CHANGED, MAKELPARAM(pBIH->biWidth, abs(pBIH->biHeight)), 0);

	SafeRelease(&pSampleOut);

	return hr;
}

HRESULT CFMS30Video::PerformFlush()
{
	CAutoLock cAutoLock(&m_csReceive);

	ReleaseLastSequenceFrame();
	m_Decoder.Flush();

	//m_bInDVDMenu = FALSE;

	if (m_pFilterGraph)
		avfilter_graph_free(&m_pFilterGraph);

	m_rtPrevStart = m_rtPrevStop = 0;
	memset(&m_FilterPrevFrame, 0, sizeof(m_FilterPrevFrame));

	return S_OK;
}

HRESULT CFMS30Video::ReleaseLastSequenceFrame()
{
	// Release DXVA2 frames hold in the last sequence frame
	if (m_pLastSequenceFrame && m_pLastSequenceFrame->format == LAVPixFmt_DXVA2) {
		IMediaSample *pSample = (IMediaSample *)m_pLastSequenceFrame->data[0];
		IDirect3DSurface9 *pSurface = (IDirect3DSurface9 *)m_pLastSequenceFrame->data[3];
		SafeRelease(&pSample);
		SafeRelease(&pSurface);
	}
	else if (m_pLastSequenceFrame && m_pLastSequenceFrame->format == LAVPixFmt_D3D11)
	{
		// TODO D3D11
	}
	ReleaseFrame(&m_pLastSequenceFrame);

	return S_OK;
}
