#pragma once

#include "IStreamSourceControl.h"

class CFMS30Splitter;

class CFMS30InputPin:public CBasePin,public CCritSec,public IStreamSourceControl
{
public:
	CFMS30InputPin(TCHAR* pName, CFMS30Splitter *pFilter, CCritSec* pLock, HRESULT* phr);
	virtual ~CFMS30InputPin(void);

	HRESULT GetAVIOContext(AVIOContext** ppContext);

	DECLARE_IUNKNOWN;
	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

	HRESULT CheckMediaType(const CMediaType* pmt);
	HRESULT CheckConnect(IPin* pPin);
	HRESULT BreakConnect();
	HRESULT CompleteConnect(IPin* pPin);

	STDMETHODIMP BeginFlush();
	STDMETHODIMP EndFlush();

	CMediaType& CurrentMediaType() { return m_mt; }

	// IStreamSourceControl
	STDMETHODIMP GetStreamDuration(REFERENCE_TIME *prtDuration) { CheckPointer(m_pStreamControl, E_NOTIMPL); return m_pStreamControl->GetStreamDuration(prtDuration); }
	STDMETHODIMP SeekStream(REFERENCE_TIME rtPosition) { CheckPointer(m_pStreamControl, E_NOTIMPL); return m_pStreamControl->SeekStream(rtPosition); }


protected:
	static int Read(void *opaque, uint8_t *buf, int buf_size);
	static int64_t Seek(void *opaque, int64_t offset, int whence);

	LONGLONG m_llPos = 0;

private:
	IAsyncReader *m_pAsyncReader = nullptr;
	AVIOContext *m_pAVIOContext = nullptr;

	IStreamSourceControl *m_pStreamControl = nullptr;

	BOOL m_bURLSource = false;
};

