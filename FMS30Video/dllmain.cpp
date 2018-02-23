// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "stdafx.h"

// Initialize the GUIDs
#include <InitGuid.h>

#include <qnetwork.h>
#include <dxva2api.h>
#include "FMS30Video.h"
#include "moreuuids.h"
#include "IMediaSideDataFFmpeg.h"

#include "BaseDSPropPage.h"

#include "registry.h"

// --- COM factory table and registration code --------------

const AMOVIESETUP_PIN sudpPinsVideoDec[] = {
	{ L"Input", FALSE, FALSE, FALSE, FALSE, &CLSID_NULL, nullptr, CFMS30Video::sudPinTypesInCount,  CFMS30Video::sudPinTypesIn },
	{ L"Output", FALSE, TRUE, FALSE, FALSE, &CLSID_NULL, nullptr, CFMS30Video::sudPinTypesOutCount, CFMS30Video::sudPinTypesOut }
};

const AMOVIESETUP_FILTER sudFilterReg =
{
	&__uuidof(CFMS30Video),       // filter clsid
	L"FMS30 Video Decoder",       // filter name
	MERIT_PREFERRED + 3,        // merit
	countof(sudpPinsVideoDec),
	sudpPinsVideoDec,
	CLSID_LegacyAmFilterCategory
};

// --- COM factory table and registration code --------------

// DirectShow base class COM factory requires this table,
// declaring all the COM objects in this DLL
CFactoryTemplate g_Templates[] = {
	// one entry for each CoCreate-able object
	{
		sudFilterReg.strName,
		sudFilterReg.clsID,
		CreateInstance<CFMS30Video>,
		CFMS30Video::StaticInit,
		&sudFilterReg
	}
};
int g_cTemplates = sizeof(g_Templates) / sizeof(g_Templates[0]);

// self-registration entrypoint
STDAPI DllRegisterServer()
{
	// base classes will handle registration using the factory template table
	return AMovieDllRegisterServer2(true);
}

STDAPI DllUnregisterServer()
{
	// base classes will handle de-registration using the factory template table
	return AMovieDllRegisterServer2(false);
}

// if we declare the correct C runtime entrypoint and then forward it to the DShow base
// classes we will be sure that both the C/C++ runtimes and the base classes are initialized
// correctly
extern "C" BOOL WINAPI DllEntryPoint(HINSTANCE, ULONG, LPVOID);
BOOL WINAPI DllMain(HANDLE hDllHandle, DWORD dwReason, LPVOID lpReserved)
{
	return DllEntryPoint(reinterpret_cast<HINSTANCE>(hDllHandle), dwReason, lpReserved);
}

void CALLBACK OpenConfiguration(HWND hwnd, HINSTANCE hinst, LPSTR lpszCmdLine, int nCmdShow)
{
	HRESULT hr = S_OK;
	CUnknown *pInstance = CreateInstance<CFMS30Video>(nullptr, &hr);
	IBaseFilter *pFilter = nullptr;
	pInstance->NonDelegatingQueryInterface(IID_IBaseFilter, (void **)&pFilter);
	if (pFilter) {
		pFilter->AddRef();
		CBaseDSPropPage::ShowPropPageDialog(pFilter);
	}
	delete pInstance;
}

