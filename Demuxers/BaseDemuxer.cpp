#include "stdafx.h"
#include "BaseDemuxer.h"

#include "moreuuids.h"

CBaseDemuxer::CBaseDemuxer(LPCTSTR pName, CCritSec *pLock)
	: CUnknown(pName, nullptr), m_pLock(pLock)
{
	for (int i = 0; i < unknown; ++i) {
		m_dActiveStreams[i] = -1;
	}
}


// CStreamList
const WCHAR* CBaseDemuxer::CStreamList::ToStringW(int type)
{
	return
		type == video ? L"Video" :
		type == audio ? L"Audio" :
		type == subpic ? L"Subtitle" :
		L"Unknown";
}

const CHAR* CBaseDemuxer::CStreamList::ToString(int type)
{
	return
		type == video ? "Video" :
		type == audio ? "Audio" :
		type == subpic ? "Subtitle" :
		"Unknown";
}

CBaseDemuxer::stream* CBaseDemuxer::CStreamList::FindStream(DWORD pid)
{
	std::deque<stream>::iterator it;
	for (it = begin(); it != end(); ++it) {
		if ((*it).pid == pid) {
			return &(*it);
		}
	}

	return nullptr;
}

void CBaseDemuxer::CStreamList::Clear()
{
	std::deque<stream>::iterator it;
	for (it = begin(); it != end(); ++it) {
		delete (*it).streamInfo;
	}
	__super::clear();
}

CBaseDemuxer::stream* CBaseDemuxer::FindStream(DWORD pid)
{
	for (int i = 0; i < StreamType::unknown; i++) {
		stream *pStream = m_streams[i].FindStream(pid);
		if (pStream)
			return pStream;
	}
	return nullptr;
}