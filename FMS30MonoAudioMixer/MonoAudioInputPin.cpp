#include "stdafx.h"
#include "MonoAudioInputPin.h"


CMonoAudioInputPin::CMonoAudioInputPin(__in_opt LPCTSTR pObjectName,
	__inout CTransformFilter *pTransformFilter,
	__inout HRESULT * phr,
	__in_opt LPCWSTR pName):CTransformInputPin(pObjectName, pTransformFilter, phr, pName)
{
	m_pRecvCall = NULL;
}


CMonoAudioInputPin::~CMonoAudioInputPin()
{
}

void CMonoAudioInputPin::SetAudioChannelPos(EnumAudioCHNPos posIn)
{
	m_audioChnPos = posIn;
}

void CMonoAudioInputPin::SetMonoAReceiveCallback(IMonoAReceiveInterface * pCall)
{
	m_pRecvCall = pCall;
}

STDMETHODIMP CMonoAudioInputPin::Receive(IMediaSample * pSample)
{
	HRESULT hr;
	ASSERT(pSample);

	// check all is well with the base class
	hr = CBaseInputPin::Receive(pSample);
	if (S_OK == hr) {
		if (m_pRecvCall)
		{
			hr = m_pRecvCall->OnAudioReceive(pSample, m_audioChnPos);
		}
		else
		{
			hr = m_pTransformFilter->Receive(pSample);

		}
	}
	return hr;
}
