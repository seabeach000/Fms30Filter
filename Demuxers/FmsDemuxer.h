#pragma once
#include "BaseDemuxer.h"
class CFmsDemuxer :
	public CBaseDemuxer
{
public:
	CFmsDemuxer(CCritSec *pLock, ILAVFSettingsInternal *settings);
	~CFmsDemuxer();

	static void ffmpeg_init(bool network);

	// IUnknown
	DECLARE_IUNKNOWN
	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

	// CBaseDemuxer
	STDMETHODIMP Open(LPCOLESTR pszFileName);
	STDMETHODIMP Start();
	STDMETHODIMP AbortOpening(int mode = 1, int timeout = 0);
	REFERENCE_TIME GetDuration() const;
	STDMETHODIMP GetNextPacket(Packet **ppPacket);
	STDMETHODIMP Seek(REFERENCE_TIME rTime);
	STDMETHODIMP Reset();
	const char *GetContainerFormat() const;
	virtual DWORD GetContainerFlags() { return  0; }


	void SettingsChanged(ILAVFSettingsInternal *pSettings);

	// Select the best video stream
	const stream* SelectVideoStream();
	// Select the best audio stream
	const stream* SelectAudioStream(std::list<std::string> prefLanguages);
	// Select the best subtitle stream
	const stream* SelectSubtitleStream(std::list<CSubtitleSelector> subtitleSelectors, std::string audioLanguage);


	const std::list<CBaseDemuxer::stream*> SelectAllAudioStream(std::list<std::string> prefLanguages);



	HRESULT SetActiveStream(StreamType type, int pid);

	STDMETHODIMP_(DWORD) GetStreamFlags(DWORD dwStream);
	STDMETHODIMP_(int) GetPixelFormat(DWORD dwStream);
	STDMETHODIMP_(int) GetHasBFrames(DWORD dwStream);
	STDMETHODIMP GetSideData(DWORD dwStream, GUID guidType, const BYTE **pData, size_t *pSize);




	STDMETHODIMP OpenInputStream(AVIOContext *byteContext, LPCOLESTR pszFileName = nullptr, const char *format = nullptr, BOOL bForce = FALSE, BOOL bFileSource = FALSE);
	STDMETHODIMP SeekByte(int64_t pos, int flags);

private:
	STDMETHODIMP AddStream(int streamId);
	STDMETHODIMP CreateStreams();
	void CleanupAVFormat();
	void UpdateParserFlags(AVStream *st);


	REFERENCE_TIME ConvertTimestampToRT(int64_t pts, int num, int den, int64_t starttime = (int64_t)AV_NOPTS_VALUE) const;
	int64_t ConvertRTToTimestamp(REFERENCE_TIME timestamp, int num, int den, int64_t starttime = (int64_t)AV_NOPTS_VALUE) const;

	static int avio_interrupt_cb(void *opaque);

	STDMETHODIMP CreatePacketMediaType(Packet *pPacket, enum AVCodecID codec_id, BYTE *extradata, int extradata_size, BYTE *paramchange, int paramchange_size);

private:
	AVFormatContext *m_avFormat = nullptr;
	const char      *m_pszInputFormat = nullptr;

	REFERENCE_TIME m_rtCurrent = 0;

	ILAVFSettingsInternal *m_pSettings = nullptr;

	int m_Abort = 0;
	time_t m_timeAbort = 0;
	time_t m_timeOpening = 0;
};

