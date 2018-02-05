// Fms30Filter.cpp : 定义 DLL 应用程序的导出函数。
//

#include "stdafx.h"
#include "LAVSplitterSettingsInternal.h"

#include "DShowUtil.h"

class MyClass:public ILAVFSettingsInternal
{
public:
	MyClass();
	~MyClass();


	// ILAVFSettings
	STDMETHODIMP SetMaxQueueMemSize(DWORD dwMaxSize);
	STDMETHODIMP_(DWORD) GetMaxQueueMemSize();
	STDMETHODIMP SetNetworkStreamAnalysisDuration(DWORD dwDuration);
	STDMETHODIMP_(DWORD) GetNetworkStreamAnalysisDuration();
	STDMETHODIMP SetMaxQueueSize(DWORD dwMaxSize);
	STDMETHODIMP_(DWORD) GetMaxQueueSize();

	// ILAVSplitterSettingsInternal
	STDMETHODIMP_(LPCSTR) GetInputFormat() { /*if (m_pDemuxer) return m_pDemuxer->GetContainerFormat();*/ return nullptr; }
	STDMETHODIMP_(CMediaType *) GetOutputMediatype(int stream);
	STDMETHODIMP_(IFilterGraph *) GetFilterGraph() { /*if (m_pGraph) { m_pGraph->AddRef(); return m_pGraph; }*/ return nullptr; }


private:

};

MyClass::MyClass()
{

}

MyClass::~MyClass()
{

}

STDMETHODIMP MyClass::SetMaxQueueMemSize(DWORD dwMaxSize)
{
	return S_OK;
}

STDMETHODIMP MyClass::SetNetworkStreamAnalysisDuration(DWORD dwDuration)
{
	return S_OK;
}

STDMETHODIMP MyClass::SetMaxQueueSize(DWORD dwMaxSize)
{
	return S_OK;
}

STDMETHODIMP_(CMediaType *) MyClass::GetOutputMediatype(int stream)
{
	return S_OK;
}

STDMETHODIMP_(DWORD) MyClass::GetMaxQueueSize()
{
	return S_OK;
}

STDMETHODIMP_(DWORD) MyClass::GetNetworkStreamAnalysisDuration()
{
	return S_OK;
}

STDMETHODIMP_(DWORD) MyClass::GetMaxQueueMemSize()
{
	return S_OK;
}

