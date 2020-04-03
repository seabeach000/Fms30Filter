// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "stdafx.h"
#include <InitGuid.h>

#include <qnetwork.h>
#include "FMS30MonoAudioMixer.h"
#include "moreuuids.h"
#include "IMediaSideDataFFmpeg.h"

#include "registry.h"
/*

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
					 )
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}
*/

const AMOVIESETUP_PIN sudpPinsAudioDec[] = {
	{ L"Input", FALSE, FALSE, FALSE, FALSE, &CLSID_NULL, nullptr, CFMS30MonoAudioMixer::sudPinTypesInCount,  CFMS30MonoAudioMixer::sudPinTypesIn },
	{ L"Output", FALSE, TRUE, FALSE, FALSE, &CLSID_NULL, nullptr, CFMS30MonoAudioMixer::sudPinTypesOutCount, CFMS30MonoAudioMixer::sudPinTypesOut }
};

const AMOVIESETUP_FILTER sudFilterReg =
{
	&__uuidof(CFMS30MonoAudioMixer),       // filter clsid
	L"FMS30 MonoAudioMixer",       // filter name
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
		CreateInstance<CFMS30MonoAudioMixer>,
		CFMS30MonoAudioMixer::StaticInit,
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

STDAPI  DllUnregisterServer()
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