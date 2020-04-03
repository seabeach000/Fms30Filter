#pragma once

#include "transfrm.h"

enum  EnumAudioCHNPos
{
	AudioChn_left = 0,
	AudioChn_right = 1
};

class IMonoAReceiveInterface
{
public:
	virtual HRESULT OnAudioReceive(IMediaSample * pSample, EnumAudioCHNPos apos) =0;
};


class CMonoAudioInputPin:public CTransformInputPin
{
public:
	CMonoAudioInputPin(__in_opt LPCTSTR pObjectName,
		__inout CTransformFilter *pTransformFilter,
		__inout HRESULT * phr,
		__in_opt LPCWSTR pName);
	virtual ~CMonoAudioInputPin();

	void SetAudioChannelPos(EnumAudioCHNPos posIn);
	void SetMonoAReceiveCallback(IMonoAReceiveInterface* pCall);
	STDMETHODIMP Receive(IMediaSample * pSample);

	EnumAudioCHNPos m_audioChnPos;
	IMonoAReceiveInterface* m_pRecvCall;
};

