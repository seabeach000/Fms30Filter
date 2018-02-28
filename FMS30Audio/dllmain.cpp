// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "stdafx.h"

// Initialize the GUIDs
#include <InitGuid.h>

#include <qnetwork.h>
#include "FMS30Audio.h"
#include "moreuuids.h"
#include "IMediaSideDataFFmpeg.h"

#include "registry.h"

// --- COM factory table and registration code --------------

const AMOVIESETUP_PIN sudpPinsAudioDec[] = {
	{ L"Input", FALSE, FALSE, FALSE, FALSE, &CLSID_NULL, nullptr, CFMS30Audio::sudPinTypesInCount,  CFMS30Audio::sudPinTypesIn },
	{ L"Output", FALSE, TRUE, FALSE, FALSE, &CLSID_NULL, nullptr, CFMS30Audio::sudPinTypesOutCount, CFMS30Audio::sudPinTypesOut }
};

const AMOVIESETUP_FILTER sudFilterReg =
{
	&__uuidof(CFMS30Audio),       // filter clsid
	L"FMS30 Audio Decoder",       // filter name
	MERIT_PREFERRED + 3,        // merit
	countof(sudpPinsAudioDec),
	sudpPinsAudioDec,
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
		CreateInstance<CFMS30Audio>,
		CFMS30Audio::StaticInit,
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