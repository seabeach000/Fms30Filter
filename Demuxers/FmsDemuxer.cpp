#include "stdafx.h"
#include "FmsDemuxer.h"
#include "LAVFUtils.h"
#include "LAVFStreamInfo.h"
#include "ILAVPinInfo.h"
#include "LAVFVideoHelper.h"
#include "ExtradataParser.h"

#include "LAVSplitterSettingsInternal.h"

#include "moreuuids.h"

#include <limits>
#include <vector>

using namespace std;

extern "C"
{
#include "libavutil/avutil.h"
#include "libavcodec/avcodec.h"
}
#ifdef DEBUG
#include "lavf_log.h"
#endif

#define AVFORMAT_OPEN_TIMEOUT 20

static const AVRational AV_RATIONAL_TIMEBASE = { 1, AV_TIME_BASE };

void CFmsDemuxer::ffmpeg_init(bool network)
{
#ifdef DEBUG
	DbgSetModuleLevel(LOG_CUSTOM1, DWORD_MAX); // FFMPEG messages use custom1
	av_log_set_callback(lavf_log_callback);
#else
	av_log_set_callback(nullptr);
#endif

	av_register_all();
	if (network)
		avformat_network_init();
}

CFmsDemuxer::CFmsDemuxer(CCritSec *pLock, ILAVFSettingsInternal *settings)
	:CBaseDemuxer(L"FMS30 demuxer", pLock)
{
	m_pSettings = settings;
}


CFmsDemuxer::~CFmsDemuxer()
{
	CleanupAVFormat();
}

STDMETHODIMP CFmsDemuxer::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
	CheckPointer(ppv, E_POINTER);

	*ppv = nullptr;

	return __super::NonDelegatingQueryInterface(riid, ppv);
}

STDMETHODIMP CFmsDemuxer::Open(LPCOLESTR pszFileName)
{
	return OpenInputStream(nullptr, pszFileName, nullptr, TRUE);
}

STDMETHODIMP CFmsDemuxer::Start()
{
	if (m_avFormat)
		av_read_play(m_avFormat);
	return S_OK;
}

STDMETHODIMP CFmsDemuxer::AbortOpening(int mode /*= 1*/, int timeout /*= 0*/)
{
	m_Abort = mode;
	m_timeAbort = timeout ? time(nullptr) + timeout : 0;
	return S_OK;
}

REFERENCE_TIME CFmsDemuxer::GetDuration() const
{
	int64_t iLength = 0;
	if (m_avFormat->duration == (int64_t)AV_NOPTS_VALUE || m_avFormat->duration < 0LL) {
		return -1;
	}
	else {
		iLength = m_avFormat->duration;
	}
	return ConvertTimestampToRT(iLength, 1, AV_TIME_BASE, 0);
}

STDMETHODIMP CFmsDemuxer::GetNextPacket(Packet **ppPacket)
{
	CheckPointer(ppPacket, E_POINTER);

	// If true, S_FALSE is returned, indicating a soft-failure
	bool bReturnEmpty = false;

	// Read packet
	AVPacket pkt;
	Packet *pPacket = nullptr;

	// assume we are not eof
	if (m_avFormat->pb) {
		m_avFormat->pb->eof_reached = 0;
	}

	int result = 0;
	try {
		DBG_TIMING("av_read_frame", 30, result = av_read_frame(m_avFormat, &pkt))
	}
	catch (...) {
		// ignore..
	}

	if (result == AVERROR_EOF)
	{
		DbgLog((LOG_TRACE, 10, L"::GetNextPacket(): End of File reached"));
	}
	else if (result == AVERROR(EIO))
	{
		DbgLog((LOG_TRACE, 10, L"::GetNextPacket():RIO assuming  End of File reached"));
	}
	else if (result < 0) {
		// meh, fail
	}
	else if (pkt.size <= 0 || pkt.stream_index < 0 || (unsigned)pkt.stream_index >= m_avFormat->nb_streams) {
		// XXX, in some cases ffmpeg returns a zero or negative packet size
		if (m_avFormat->pb && !m_avFormat->pb->eof_reached) {
			bReturnEmpty = true;
		}
		av_packet_unref(&pkt);
	}
	else
	{
		// Check right here if the stream is active, we can drop the package otherwise.
		AVStream *stream = m_avFormat->streams[pkt.stream_index];
		BOOL streamActive = FALSE;
		for (int i = 0; i < unknown; ++i) {
			if (m_dActiveStreams[i].find(pkt.stream_index) != m_dActiveStreams[i].end()) {
				streamActive = TRUE;
				break;
			}
		}

		if (!streamActive)
		{
			av_packet_unref(&pkt);
			return S_FALSE;
		}

		pPacket = new Packet;
		if (!pPacket)
			return E_OUTOFMEMORY;

		// Convert timestamps to reference time and set them on the packet
		REFERENCE_TIME pts = ConvertTimestampToRT(pkt.pts, stream->time_base.num, stream->time_base.den);
		REFERENCE_TIME dts = ConvertTimestampToRT(pkt.dts, stream->time_base.num, stream->time_base.den);
		REFERENCE_TIME duration = ConvertTimestampToRT(pkt.duration, stream->time_base.num, stream->time_base.den, 0);

		pPacket->rtPTS = pts;
		pPacket->rtDTS = dts;
		pPacket->StreamId = (DWORD)pkt.stream_index;
		pPacket->bPosition = pkt.pos;

		if (pkt.data)
		{
			result = pPacket->SetPacket(&pkt);
			if (result < 0)
			{
				SAFE_DELETE(pPacket);
				return E_OUTOFMEMORY;
			}
		}

		// Select the appropriate timestamps
		REFERENCE_TIME rt = Packet::INVALID_TIME;
		// Try the different times set, pts first, dts when pts is not valid
		if (pts != Packet::INVALID_TIME) {
			rt = pts;
		}
		else if (dts != Packet::INVALID_TIME) {
			rt = dts;
		}

		pPacket->rtStart = pPacket->rtStop = rt;
		if (rt != Packet::INVALID_TIME) {
			pPacket->rtStop += (duration > 0 || stream->codecpar->codec_id == AV_CODEC_ID_TRUEHD) ? duration : 1;
		}

		if (stream->codecpar->codec_id == AV_CODEC_ID_PCM_S16BE_PLANAR
			|| stream->codecpar->codec_id == AV_CODEC_ID_PCM_S16LE_PLANAR
			|| stream->codecpar->codec_id == AV_CODEC_ID_PCM_S24LE_PLANAR
			|| stream->codecpar->codec_id == AV_CODEC_ID_PCM_S32LE_PLANAR
			)
			pPacket->dwFlags |= LAV_PACKET_PLANAR_PCM;

		// Update extradata and send new mediatype, when required
		int sidedata_size = 0;
		uint8_t *sidedata = av_packet_get_side_data(&pkt, AV_PKT_DATA_NEW_EXTRADATA, &sidedata_size);
		int paramchange_size = 0;
		uint8_t *paramchange = av_packet_get_side_data(&pkt, AV_PKT_DATA_PARAM_CHANGE, &paramchange_size);
		if ((sidedata && sidedata_size) || (paramchange && paramchange_size)) {
			CreatePacketMediaType(pPacket, stream->codecpar->codec_id, sidedata, sidedata_size, paramchange, paramchange_size);
		}

		pPacket->bSyncPoint = pkt.flags & AV_PKT_FLAG_KEY;
		pPacket->bDiscontinuity = pkt.flags & AV_PKT_FLAG_CORRUPT;
#ifdef DEBUG
		if (pkt.flags & AV_PKT_FLAG_CORRUPT)
			DbgLog((LOG_TRACE, 10, L"::GetNextPacket() - Signaling Discontinuinty because of corrupt package"));
#endif
		if (pPacket->rtStart != AV_NOPTS_VALUE)
			m_rtCurrent = pPacket->rtStart;

		av_packet_unref(&pkt);
	}

	if (bReturnEmpty && !pPacket)
		return S_FALSE;

	if (!pPacket)
	{
		return E_FAIL;
	}
	*ppPacket = pPacket;
	return S_OK;
}

inline static int init_parser(AVFormatContext *s, AVStream *st) {
	if (!st->parser && st->need_parsing && !(s->flags & AVFMT_FLAG_NOPARSE)) {
		st->parser = av_parser_init(st->codecpar->codec_id);
		if (st->parser) {
			if (st->need_parsing == AVSTREAM_PARSE_HEADERS) {
				st->parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;
			}
			else if (st->need_parsing == AVSTREAM_PARSE_FULL_ONCE) {
				st->parser->flags |= PARSER_FLAG_ONCE;
			}
		}
		else {
			return -1;
		}
	}
	return 0;
}

STDMETHODIMP CFmsDemuxer::Seek(REFERENCE_TIME rTime)
{
	int seekStreamId = GetActiveStreamId(video);
	int seek_pts = 0;

retry:
	// If we have a video stream, seek on that one. If we don't, well, then don't!
	if (rTime > 0) {
		if (seekStreamId != -1) {
			AVStream *stream = m_avFormat->streams[seekStreamId];
			seek_pts = ConvertRTToTimestamp(rTime, stream->time_base.num, stream->time_base.den);
		}
		else {
			seek_pts = ConvertRTToTimestamp(rTime, 1, AV_TIME_BASE);
		}
	}

	if (seek_pts < 0)
		seek_pts = 0;

	//使用我们famous里面的seek方式，这样出来的不是I帧
	//const int	default_stream_index_ = av_find_default_stream_index(m_avFormat);
	//AVStream *stream = m_avFormat->streams[default_stream_index_];

	//int ret = avformat_seek_file(
	//	m_avFormat,
	//	default_stream_index_,
	//	std::numeric_limits<int64_t>::lowest(),
	//	static_cast<int64_t>(seek_pts + stream->start_time),
	//	std::numeric_limits<int64_t>::infinity(),
	//	0);

	//if(ret < 0)
	//	DbgLog((LOG_CUSTOM1, 1, L"::Seek() -- Key-Frame Seek failed"));

	if (strcmp(m_pszInputFormat, "rawvideo") == 0 && seek_pts == 0)
		return SeekByte(0, AVSEEK_FLAG_BACKWARD);

	int flags = AVSEEK_FLAG_BACKWARD;

	int ret = av_seek_frame(m_avFormat, seekStreamId, seek_pts, flags);
	if (ret < 0) {
		DbgLog((LOG_CUSTOM1, 1, L"::Seek() -- Key-Frame Seek failed"));
		ret = av_seek_frame(m_avFormat, seekStreamId, seek_pts, flags | AVSEEK_FLAG_ANY);
		if (ret < 0) {
			DbgLog((LOG_ERROR, 1, L"::Seek() -- Inaccurate Seek failed as well"));
			if (seekStreamId == GetActiveStreamId(video) && seekStreamId != -1 && GetActiveStreamId(audio) != -1) {
				DbgLog((LOG_ERROR, 1, L"::Seek() -- retrying seek on audio stream"));
				seekStreamId = GetActiveStreamId(audio);
				goto retry;
			}
			if (seek_pts == 0) {
				DbgLog((LOG_ERROR, 1, L" -> attempting byte seek to position 0"));
				return SeekByte(0, AVSEEK_FLAG_BACKWARD);
			}
		}
	}
	for (unsigned int i = 0; i < m_avFormat->nb_streams; i++)
	{
		init_parser(m_avFormat, m_avFormat->streams[i]);
		UpdateParserFlags(m_avFormat->streams[i]);
	}

	return S_OK;
}

STDMETHODIMP CFmsDemuxer::Reset()
{
	return Seek(0);
}

const char * CFmsDemuxer::GetContainerFormat() const
{
	return m_pszInputFormat;
}

void CFmsDemuxer::SettingsChanged(ILAVFSettingsInternal *pSettings)
{
	//todo
}

// Select the best video stream
const CBaseDemuxer::stream* CFmsDemuxer::SelectVideoStream()
{
	const stream *best = nullptr;
	CStreamList *streams = GetStreams(video);

	std::deque<stream>::iterator it;
	for (it = streams->begin();it != streams->end();++it)
	{
		stream *check = &*it;

		if (m_avFormat->streams[check->pid]->disposition & AV_DISPOSITION_DEFAULT) {
			best = check;
			break;
		}
		if (!best)
		{
			best = check;
			continue;
		}
		uint64_t bestPixels = (uint64_t)m_avFormat->streams[best->pid]->codecpar->width * m_avFormat->streams[best->pid]->codecpar->height;
		uint64_t checkPixels = (uint64_t)m_avFormat->streams[check->pid]->codecpar->width * m_avFormat->streams[check->pid]->codecpar->height;

		if (m_avFormat->streams[best->pid]->codecpar->codec_id == AV_CODEC_ID_NONE && m_avFormat->streams[check->pid]->codecpar->codec_id != AV_CODEC_ID_NONE) {
			best = check;
			continue;
		}

		int check_nb_f = m_avFormat->streams[check->pid]->codec_info_nb_frames;
		int best_nb_f = m_avFormat->streams[best->pid]->codec_info_nb_frames;
		if (check_nb_f > 0)
		{
			if (checkPixels > bestPixels)
				best = check;
		}
	}

	return best;
}


const std::list<CBaseDemuxer::stream*> CFmsDemuxer::SelectAllAudioStream(std::list<std::string> prefLanguages)
{
	const stream *best = nullptr;
	CStreamList *streams = GetStreams(audio);

	std::deque<stream*> checkedStreams;

	std::list<CBaseDemuxer::stream*> lstRet;
	// If no language was set, or no matching streams were found
	// Put all streams in there
	if (checkedStreams.empty()) {
		std::deque<stream>::iterator sit;
		for (sit = streams->begin(); sit != streams->end(); ++sit) {
			checkedStreams.push_back(&*sit);
		}
	}

	// Check for a stream with a default flag
	// If in our current set is one, that one prevails
	std::deque<stream*>::iterator sit;
	for (sit = checkedStreams.begin(); sit != checkedStreams.end(); ++sit) {
		
			lstRet.push_back(*sit);
		
	}

	return lstRet;
}


//wxg20180130暂时只考虑一个音频流
const CBaseDemuxer::stream* CFmsDemuxer::SelectAudioStream(std::list<std::string> prefLanguages)
{
	const stream *best = nullptr;
	CStreamList *streams = GetStreams(audio);

	std::deque<stream*> checkedStreams;

	// If no language was set, or no matching streams were found
	// Put all streams in there
	if (checkedStreams.empty()) {
		std::deque<stream>::iterator sit;
		for (sit = streams->begin(); sit != streams->end(); ++sit) {
			checkedStreams.push_back(&*sit);
		}
	}

	// Check for a stream with a default flag
	// If in our current set is one, that one prevails
	std::deque<stream*>::iterator sit;
	for (sit = checkedStreams.begin(); sit != checkedStreams.end(); ++sit) {
		if (m_avFormat->streams[(*sit)->pid]->disposition & AV_DISPOSITION_DEFAULT) {
			best = *sit;
			break;
		}
	}

	if (!best && checkedStreams.size() >= 1)
		best = checkedStreams.at(0);

	return best;
}

static inline bool does_language_match(std::string selector, std::string selectee)
{
	return (selector == "*" || selector == selectee);
}

const CBaseDemuxer::stream* CFmsDemuxer::SelectSubtitleStream(std::list<CSubtitleSelector> subtitleSelectors, std::string audioLanguage)
{
	const stream *best = nullptr;
	CStreamList *streams = GetStreams(subpic);

	std::deque<stream*> checkedStreams;

	std::list<CSubtitleSelector>::iterator it = subtitleSelectors.begin();
	for (it = subtitleSelectors.begin(); it != subtitleSelectors.end() && checkedStreams.empty(); it++) {

		if (!does_language_match(it->audioLanguage, audioLanguage))
			continue;

		if (it->subtitleLanguage == "off")
			break;

		std::deque<stream>::iterator sit;
		for (sit = streams->begin(); sit != streams->end(); sit++) {
			if (sit->pid == NO_SUBTITLE_PID)
				continue;

			if (!it->subtitleTrackName.empty() && sit->trackName.find(it->subtitleTrackName) == std::string::npos)
				continue;

			if (sit->pid == FORCED_SUBTITLE_PID) {
				if ((it->dwFlags == 0 || it->dwFlags & SUBTITLE_FLAG_VIRTUAL) && does_language_match(it->subtitleLanguage, audioLanguage))
					checkedStreams.push_back(&*sit);
				continue;
			}

			if (it->dwFlags == 0
				|| ((it->dwFlags & SUBTITLE_FLAG_DEFAULT) && (m_avFormat->streams[sit->pid]->disposition & AV_DISPOSITION_DEFAULT))
				|| ((it->dwFlags & SUBTITLE_FLAG_FORCED) && (m_avFormat->streams[sit->pid]->disposition & AV_DISPOSITION_FORCED))
				|| ((it->dwFlags & SUBTITLE_FLAG_IMPAIRED) && (m_avFormat->streams[sit->pid]->disposition & (AV_DISPOSITION_HEARING_IMPAIRED | AV_DISPOSITION_VISUAL_IMPAIRED)))
				|| ((it->dwFlags & SUBTITLE_FLAG_PGS) && (m_avFormat->streams[sit->pid]->codecpar->codec_id == AV_CODEC_ID_HDMV_PGS_SUBTITLE))
				|| ((it->dwFlags & SUBTITLE_FLAG_NORMAL)
					&& !(m_avFormat->streams[sit->pid]->disposition & (AV_DISPOSITION_DEFAULT | AV_DISPOSITION_FORCED | AV_DISPOSITION_HEARING_IMPAIRED | AV_DISPOSITION_VISUAL_IMPAIRED)))) {
				std::string streamLanguage = sit->language;
				if (does_language_match(it->subtitleLanguage, streamLanguage))
					checkedStreams.push_back(&*sit);
			}
		}
	}

	if (!checkedStreams.empty())
		best = streams->FindStream(checkedStreams.front()->pid);
	else
		best = streams->FindStream(NO_SUBTITLE_PID);

	return best;
}

HRESULT CFmsDemuxer::SetActiveStream(StreamType type, int pid)
{
	HRESULT hr = S_OK;

	hr = __super::SetActiveStream(type, pid);
	
	return hr;
}

STDMETHODIMP_(DWORD) CFmsDemuxer::GetStreamFlags(DWORD dwStream)
{
	if (!m_avFormat || dwStream >= m_avFormat->nb_streams)
		return 0;

	DWORD dwFlags = 0;
	AVStream *st = m_avFormat->streams[dwStream];

	if (strcmp(m_pszInputFormat, "rawvideo") == 0)
		dwFlags |= LAV_STREAM_FLAG_ONLY_DTS;

	return dwFlags;
}

STDMETHODIMP_(int) CFmsDemuxer::GetPixelFormat(DWORD dwStream)
{
	if (!m_avFormat || dwStream >= m_avFormat->nb_streams)
		return AV_PIX_FMT_NONE;

	return m_avFormat->streams[dwStream]->codecpar->format;
}

STDMETHODIMP_(int) CFmsDemuxer::GetHasBFrames(DWORD dwStream)
{
	if (!m_avFormat || dwStream >= m_avFormat->nb_streams)
		return -1;

	return m_avFormat->streams[dwStream]->codecpar->video_delay;
}

STDMETHODIMP CFmsDemuxer::GetSideData(DWORD dwStream, GUID guidType, const BYTE **pData, size_t *pSize)
{
	if (!m_avFormat || dwStream >= m_avFormat->nb_streams)
		return E_INVALIDARG;

	if (guidType == IID_MediaSideDataFFMpeg) {
		CBaseDemuxer::stream *pStream = FindStream(dwStream);
		if (!pStream)
			return E_FAIL;

		pStream->SideData.side_data = m_avFormat->streams[dwStream]->side_data;
		pStream->SideData.side_data_elems = m_avFormat->streams[dwStream]->nb_side_data;
		*pData = (BYTE*)&pStream->SideData;
		*pSize = sizeof(pStream->SideData);

		return S_OK;
	}

	return E_INVALIDARG;
}

STDMETHODIMP CFmsDemuxer::OpenInputStream(AVIOContext *byteContext, LPCOLESTR pszFileName /*= nullptr*/, const char *format /*= nullptr*/, BOOL bForce /*= FALSE*/, BOOL bFileSource /*= FALSE*/)
{
	CAutoLock lock(m_pLock);
	HRESULT hr = S_OK;

	int ret;

	// Convert the filename from wchar to char for avformat
	char fileName[4100] = { 0 };
	if (pszFileName) {
		ret = SafeWideCharToMultiByte(CP_UTF8, 0, pszFileName, -1, fileName, 4096, nullptr, nullptr);
	}

	AVIOInterruptCB cb = { avio_interrupt_cb, this };

	m_avFormat = avformat_alloc_context();
	m_avFormat->probesize = m_avFormat->probesize * 4;
	m_avFormat->interrupt_callback = cb;

	AVDictionary* format_options = nullptr;
	AVInputFormat *inputFormat = nullptr;
	m_timeOpening = time(nullptr);
	ret = avformat_open_input(&m_avFormat, fileName, inputFormat, &format_options);
	av_dict_free(&format_options);
	if (ret < 0)
	{
		DbgLog((LOG_ERROR, 0, TEXT("::OpenInputStream(): avformat_open_input failed (%d)"), ret));
		goto done;
	}
	DbgLog((LOG_TRACE, 10, TEXT("::OpenInputStream(): avformat_open_input opened file of type '%S' (took %I64d seconds)"), m_avFormat->iformat->name, time(nullptr) - m_timeOpening));
	m_timeOpening = 0;

	//InitAVFormat
	m_pszInputFormat = format ? format : m_avFormat->iformat->name;
	// preserve side-data in the packets properly
	m_avFormat->flags |= AVFMT_FLAG_KEEP_SIDE_DATA;
	m_timeOpening = time(nullptr);
	ret = avformat_find_stream_info(m_avFormat, nullptr);
	if (ret < 0) {
		DbgLog((LOG_ERROR, 0, TEXT("::InitAVFormat(): av_find_stream_info failed (%d)"), ret));
		goto done;
	}
	DbgLog((LOG_TRACE, 10, TEXT("::InitAVFormat(): avformat_find_stream_info finished, took %I64d seconds"), time(nullptr) - m_timeOpening));
	m_timeOpening = 0;

	//
	CHECK_HR(hr = CreateStreams());
	return S_OK;
done:
	CleanupAVFormat();
	return E_FAIL;
}

STDMETHODIMP CFmsDemuxer::SeekByte(int64_t pos, int flags)
{
	int ret = av_seek_frame(m_avFormat, -1, pos, flags | AVSEEK_FLAG_BYTE);
	if (ret < 0) {
		DbgLog((LOG_ERROR, 1, L"::SeekByte() -- Seek failed"));
	}

	for (unsigned i = 0; i < m_avFormat->nb_streams; i++) {
		init_parser(m_avFormat, m_avFormat->streams[i]);
		UpdateParserFlags(m_avFormat->streams[i]);
	}

	return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// Internal Functions
STDMETHODIMP CFmsDemuxer::AddStream(int streamId)
{
	HRESULT hr = S_OK;
	AVStream *pStream = m_avFormat->streams[streamId];
	if (pStream->codecpar->codec_type == AVMEDIA_TYPE_UNKNOWN
		|| pStream->discard == AVDISCARD_ALL
		|| pStream->codecpar->codec_id == AV_CODEC_ID_NONE && pStream->codecpar->codec_tag == 0)
	{
		pStream->discard = AVDISCARD_ALL;
	}

	stream s;
	s.pid = streamId;

	// Extract language
	const char *lang = nullptr;
	if (AVDictionaryEntry *dictEntry = av_dict_get(pStream->metadata, "language", nullptr, 0)) {
		lang = dictEntry->value;
	}
	if (lang) {
		s.language = ProbeForISO6392(lang);
		s.lcid = ProbeLangForLCID(s.language.c_str());
	}
	else {
		s.language = "und";
		s.lcid = 0;
	}
	const char * title = lavf_get_stream_title(pStream);
	if (title)
		s.trackName = title;
	s.streamInfo = new CLAVFStreamInfo(m_avFormat, pStream, m_pszInputFormat, hr);

	if (hr != S_OK) {
		delete s.streamInfo;
		pStream->discard = AVDISCARD_ALL;
		return hr;
	}

	switch (pStream->codecpar->codec_type)
	{
	case AVMEDIA_TYPE_VIDEO:
		m_streams[video].push_back(s);
		break;
	case AVMEDIA_TYPE_AUDIO:
		m_streams[audio].push_back(s);
		break;
	case AVMEDIA_TYPE_SUBTITLE:
		m_streams[subpic].push_back(s);
		break;
	default:
		// unsupported stream
		// Normally this should be caught while creating the stream info already.
		delete s.streamInfo;
		return E_FAIL;
	}
	return S_OK;
}

STDMETHODIMP CFmsDemuxer::CreateStreams()
{
	DbgLog((LOG_TRACE, 10, L"CFmsDemuxer::CreateStreams()"));
	CAutoLock lock(m_pLock);

	for (int i = 0; i < countof(m_streams); ++i) {
		m_streams[i].Clear();
	}

	// Re-compute the overall file duration based on video and audio durations
	int64_t duration = INT64_MIN;
	int64_t st_duration = 0;
	int64_t start_time = INT64_MAX;
	int64_t st_start_time = 0;

	// Number of streams (either in file or in program)	
	unsigned int nbIndex =  m_avFormat->nb_streams;
	// add streams from selected program, or all streams if no program was selected
	for (unsigned int i = 0;i<nbIndex;i++)
	{
		int streamIdx = i;
		if (S_OK != AddStream(streamIdx))
			continue;

		AVStream *st = m_avFormat->streams[streamIdx];
		if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO || st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			if (st->duration != AV_NOPTS_VALUE)
			{
				st->duration = av_rescale_q(st->duration, st->time_base, AV_RATIONAL_TIMEBASE);
				if (st_duration > duration)
					duration = st_duration;
			}

			if (st->start_time != AV_NOPTS_VALUE)
			{
				st_start_time = av_rescale_q(st->start_time, st->time_base, AV_RATIONAL_TIMEBASE);
				if (st_start_time < start_time)
					start_time = st_start_time;
			}
		}
	}

	return S_OK;
}

void CFmsDemuxer::CleanupAVFormat()
{
	if (m_avFormat) {
		// Override abort timer to ensure the close function in network protocols can actually close the stream
		AbortOpening(1, 5);
		avformat_close_input(&m_avFormat);
	}
}

void CFmsDemuxer::UpdateParserFlags(AVStream *st)
{
	//if (st->parser) {
	//	if ((st->codecpar->codec_id == AV_CODEC_ID_MPEG2VIDEO || st->codecpar->codec_id == AV_CODEC_ID_MPEG1VIDEO) && _stricmp(m_pszInputFormat, "mpegvideo") != 0) {
	//		st->parser->flags |= PARSER_FLAG_NO_TIMESTAMP_MANGLING;
	//	}
	//	else if (st->codecpar->codec_id == AV_CODEC_ID_H264) {
	//		st->parser->flags |= PARSER_FLAG_NO_TIMESTAMP_MANGLING;
	//	}
	//}
}

// Converts the lavf pts timestamp to a DShow REFERENCE_TIME
// Based on DVDDemuxFFMPEG
REFERENCE_TIME CFmsDemuxer::ConvertTimestampToRT(int64_t pts, int num, int den, int64_t starttime /*= (int64_t)AV_NOPTS_VALUE*/) const
{
	if (pts == (int64_t)AV_NOPTS_VALUE) {
		return Packet::INVALID_TIME;
	}

	if (starttime == AV_NOPTS_VALUE) {
		if (m_avFormat->start_time != AV_NOPTS_VALUE) {
			starttime = av_rescale(m_avFormat->start_time, den, (int64_t)AV_TIME_BASE * num);
		}
		else {
			starttime = 0;
		}
	}

	if (starttime != 0) {
		pts -= starttime;
	}

	// Let av_rescale do the work, its smart enough to not overflow
	REFERENCE_TIME timestamp = av_rescale(pts, (int64_t)num * DSHOW_TIME_BASE, den);

	return timestamp;
}

int64_t CFmsDemuxer::ConvertRTToTimestamp(REFERENCE_TIME timestamp, int num, int den, int64_t starttime /*= (int64_t)AV_NOPTS_VALUE*/) const
{
	if (timestamp == Packet::INVALID_TIME) {
		return (int64_t)AV_NOPTS_VALUE;
	}

	if (starttime == AV_NOPTS_VALUE) {
		if (m_avFormat->start_time != AV_NOPTS_VALUE) {
			starttime = av_rescale(m_avFormat->start_time, den, (int64_t)AV_TIME_BASE * num);
		}
		else {
			starttime = 0;
		}
	}

	int64_t pts = av_rescale(timestamp, den, (int64_t)num * DSHOW_TIME_BASE);
	if (starttime != 0) {
		pts += starttime;
	}

	return pts;
}

int CFmsDemuxer::avio_interrupt_cb(void *opaque)
{
	CFmsDemuxer *demux = (CFmsDemuxer *)opaque;

	// Check for file opening timeout
	time_t now = time(nullptr);
	if (demux->m_timeOpening && now > (demux->m_timeOpening + AVFORMAT_OPEN_TIMEOUT))
		return 1;

	if (demux->m_Abort && now > demux->m_timeAbort)
		return 1;

	return 0;
}

#define VC1_CODE_RES0 0x00000100
#define IS_VC1_MARKER(x) (((x) & ~0xFF) == VC1_CODE_RES0)

STDMETHODIMP CFmsDemuxer::CreatePacketMediaType(Packet *pPacket, enum AVCodecID codec_id, BYTE *extradata, int extradata_size, BYTE *paramchange, int paramchange_size)
{
	CMediaType *pmt = m_pSettings->GetOutputMediatype(pPacket->StreamId);
	if (pmt)
	{
		if (extradata && extradata_size)
		{
			if (codec_id == AV_CODEC_ID_H264)
			{
				MPEG2VIDEOINFO *mp2vi = (MPEG2VIDEOINFO *)pmt->ReallocFormatBuffer(sizeof(MPEG2VIDEOINFO) + extradata_size);
				int ret = g_VideoHelper.ProcessH264Extradata(extradata, extradata_size, mp2vi, FALSE);
				if (ret < 0) {
					mp2vi->cbSequenceHeader = extradata_size;
					memcpy(&mp2vi->dwSequenceHeader[0], extradata, extradata_size);
				}
				else {
					int mp2visize = SIZE_MPEG2VIDEOINFO(mp2vi);
					memset((BYTE *)mp2vi + mp2visize, 0, pmt->cbFormat - mp2visize);
				}
			} else if (codec_id == AV_CODEC_ID_MPEG2VIDEO) {
				MPEG2VIDEOINFO *mp2vi = (MPEG2VIDEOINFO *)pmt->ReallocFormatBuffer(sizeof(MPEG2VIDEOINFO) + extradata_size);
				CExtradataParser parser = CExtradataParser(extradata, extradata_size);
				mp2vi->cbSequenceHeader = (DWORD)parser.ParseMPEGSequenceHeader((BYTE *)&mp2vi->dwSequenceHeader[0]);
			}
			else if (codec_id == AV_CODEC_ID_VC1) {
				VIDEOINFOHEADER2 *vih2 = (VIDEOINFOHEADER2 *)pmt->ReallocFormatBuffer(sizeof(VIDEOINFOHEADER2) + extradata_size + 1);
				int i = 0;
				for (i = 0; i < (extradata_size - 4); i++) {
					uint32_t code = AV_RB32(extradata + i);
					if (IS_VC1_MARKER(code))
						break;
				}
				if (i == 0) {
					*((BYTE*)vih2 + sizeof(VIDEOINFOHEADER2)) = 0;
					memcpy((BYTE*)vih2 + sizeof(VIDEOINFOHEADER2) + 1, extradata, extradata_size);
				}
				else {
					memcpy((BYTE*)vih2 + sizeof(VIDEOINFOHEADER2), extradata, extradata_size);
				}
			}
			else if (codec_id == AV_CODEC_ID_SSA) {
				SUBTITLEINFO *sif = (SUBTITLEINFO *)pmt->ReallocFormatBuffer(sizeof(SUBTITLEINFO) + extradata_size);
				memcpy((BYTE *)sif + sizeof(SUBTITLEINFO), extradata, extradata_size);
			}
			else {
				if (pmt->formattype == FORMAT_VideoInfo) {
					VIDEOINFOHEADER *vih = (VIDEOINFOHEADER *)pmt->ReallocFormatBuffer(sizeof(VIDEOINFOHEADER) + extradata_size);
					vih->bmiHeader.biSize = sizeof(BITMAPINFOHEADER) + extradata_size;
					memcpy((BYTE*)vih + sizeof(VIDEOINFOHEADER), extradata, extradata_size);
				}
				else if (pmt->formattype == FORMAT_VideoInfo2) {
					VIDEOINFOHEADER2 *vih2 = (VIDEOINFOHEADER2 *)pmt->ReallocFormatBuffer(sizeof(VIDEOINFOHEADER2) + extradata_size);
					vih2->bmiHeader.biSize = sizeof(BITMAPINFOHEADER) + extradata_size;
					memcpy((BYTE*)vih2 + sizeof(VIDEOINFOHEADER2), extradata, extradata_size);
				}
				else if (pmt->formattype == FORMAT_WaveFormatEx) {
					WAVEFORMATEX *wfex = (WAVEFORMATEX *)pmt->ReallocFormatBuffer(sizeof(WAVEFORMATEX) + extradata_size);
					wfex->cbSize = extradata_size;
					memcpy((BYTE*)wfex + sizeof(WAVEFORMATEX), extradata, extradata_size);
				}
				else if (pmt->formattype == FORMAT_WaveFormatExFFMPEG) {
					WAVEFORMATEXFFMPEG *wfex = (WAVEFORMATEXFFMPEG *)pmt->ReallocFormatBuffer(sizeof(WAVEFORMATEXFFMPEG) + extradata_size);
					wfex->wfex.cbSize = extradata_size;
					memcpy((BYTE*)wfex + sizeof(WAVEFORMATEXFFMPEG), extradata, extradata_size);
				}
				else if (pmt->formattype == FORMAT_VorbisFormat2) {
					BYTE *p = extradata;
					std::vector<int> sizes;
					for (BYTE n = *p++; n > 0; n--) {
						int size = 0;
						// Xiph Lacing
						do { size += *p; } while (*p++ == 0xFF);
						sizes.push_back(size);
					}

					int totalsize = 0;
					for (size_t i = 0; i < sizes.size(); i++)
						totalsize += sizes[i];

					sizes.push_back(extradata_size - (int)(p - extradata) - totalsize);
					totalsize += sizes[sizes.size() - 1];

					// 3 blocks is the currently valid Vorbis format
					if (sizes.size() == 3) {
						VORBISFORMAT2* pvf2 = (VORBISFORMAT2*)pmt->ReallocFormatBuffer(sizeof(VORBISFORMAT2) + totalsize);
						BYTE *p2 = (BYTE *)pvf2 + sizeof(VORBISFORMAT2);
						for (unsigned int i = 0; i < sizes.size(); p += sizes[i], p2 += sizes[i], i++) {
							memcpy(p2, p, pvf2->HeaderSize[i] = sizes[i]);
						}
					}
				}
				else {
					DbgLog((LOG_TRACE, 10, L"::GetNextPacket() - Unsupported PMT change on codec %S", avcodec_get_name(codec_id)));
				}
			}
		}
		if (paramchange)
		{
			uint32_t flags = AV_RL32(paramchange);
			int channels = 0, sample_rate = 0, width = 0, height = 0, aspect_num = 0, aspect_den = 0;
			paramchange += 4;
			if (flags & AV_SIDE_DATA_PARAM_CHANGE_CHANNEL_COUNT) {
				channels = AV_RL32(paramchange);
				paramchange += 4;
			}
			if (flags & AV_SIDE_DATA_PARAM_CHANGE_CHANNEL_LAYOUT) {
				paramchange += 8;
			}
			if (flags & AV_SIDE_DATA_PARAM_CHANGE_SAMPLE_RATE) {
				sample_rate = AV_RL32(paramchange);
				paramchange += 4;
			}
			if (flags & AV_SIDE_DATA_PARAM_CHANGE_DIMENSIONS) {
				width = AV_RL32(paramchange);
				height = AV_RL32(paramchange + 4);
				paramchange += 8;
			}
			if (pmt->majortype == MEDIATYPE_Video) {
				if ((pmt->formattype == FORMAT_VideoInfo || pmt->formattype == FORMAT_MPEGVideo) && width && height) {
					VIDEOINFOHEADER *vih = (VIDEOINFOHEADER *)pmt->pbFormat;
					vih->bmiHeader.biWidth = width;
					vih->bmiHeader.biHeight = height;
					vih->rcTarget.right = vih->rcSource.right = width;
					vih->rcTarget.bottom = vih->rcSource.bottom = height;
				}
				else if ((pmt->formattype == FORMAT_VideoInfo2 || pmt->formattype == FORMAT_MPEG2Video) && ((width && height) || (aspect_num && aspect_den))) {
					VIDEOINFOHEADER2 *vih2 = (VIDEOINFOHEADER2 *)pmt->pbFormat;
					if (width && height) {
						vih2->bmiHeader.biWidth = width;
						vih2->bmiHeader.biHeight = height;
						vih2->rcTarget.right = vih2->rcSource.right = width;
						vih2->rcTarget.bottom = vih2->rcSource.bottom = height;
					}
					if (aspect_num && aspect_den) {
						int num = vih2->bmiHeader.biWidth, den = vih2->bmiHeader.biHeight;
						av_reduce(&num, &den, (int64_t)aspect_num * num, (int64_t)aspect_den * den, INT_MAX);
						vih2->dwPictAspectRatioX = num;
						vih2->dwPictAspectRatioY = den;
					}
				}
			}
			else if (pmt->majortype == MEDIATYPE_Audio) {
				if ((pmt->formattype == FORMAT_WaveFormatEx || pmt->formattype == FORMAT_WaveFormatExFFMPEG) && (channels || sample_rate)) {
					WAVEFORMATEX *wfex = nullptr;
					if (pmt->formattype == FORMAT_WaveFormatExFFMPEG) {
						WAVEFORMATEXFFMPEG *wfexff = (WAVEFORMATEXFFMPEG *)pmt->pbFormat;
						wfex = &wfexff->wfex;
					}
					else {
						wfex = (WAVEFORMATEX *)pmt->pbFormat;
					}
					if (channels)
						wfex->nChannels = channels;
					if (sample_rate)
						wfex->nSamplesPerSec = sample_rate;
				}
			}
		}

		if (pmt) {
			pPacket->pmt = CreateMediaType(pmt);
			SAFE_DELETE(pmt);
		}
	}
	return S_OK;
}

