// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "stdafx.h"

#include "FMS30Demultiplexer.h"

// The GUID we use to register the splitter media types
// {A77A2527-B694-4177-8BB7-C4638F19717D}
DEFINE_GUID(MEDIATYPE_FMS30Splitter,
	0xa77a2527, 0xb694, 0x4177, 0x8b, 0xb7, 0xc4, 0x63, 0x8f, 0x19, 0x71, 0x7d);

// --- COM factory table and registration code --------------

const AMOVIESETUP_MEDIATYPE
sudMediaTypes[] = {
	{ &MEDIATYPE_Stream, &MEDIASUBTYPE_NULL },
};

const AMOVIESETUP_PIN sudOutputPins[] =
{
	{
		L"Output",            // pin name
		FALSE,              // is rendered?    
		TRUE,               // is output?
		FALSE,              // zero instances allowed?
		TRUE,               // many instances allowed?
		&CLSID_NULL,        // connects to filter (for bridge pins)
		nullptr,            // connects to pin (for bridge pins)
		0,                  // count of registered media types
		nullptr             // list of registered media types
	},
	{
		L"Input",             // pin name
		FALSE,              // is rendered?    
		FALSE,              // is output?
		FALSE,              // zero instances allowed?
		FALSE,              // many instances allowed?
		&CLSID_NULL,        // connects to filter (for bridge pins)
		nullptr,            // connects to pin (for bridge pins)
		1,                  // count of registered media types
		&sudMediaTypes[0]   // list of registered media types
	}
};

const AMOVIESETUP_FILTER sudFilterReg =
{
	&__uuidof(CFMS30Splitter),        // filter clsid
	L"FMS30 Splitter",                // filter name
	MERIT_PREFERRED + 4,            // merit
	2,                              // count of registered pins
	sudOutputPins,                  // list of pins to register
	CLSID_LegacyAmFilterCategory
};

const AMOVIESETUP_FILTER sudFilterRegSource =
{
	&__uuidof(CFMS30SplitterSource),  // filter clsid
	L"FMS30 Splitter Source",         // filter name
	MERIT_PREFERRED + 4,            // merit
	1,                              // count of registered pins
	sudOutputPins,                  // list of pins to register
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
		CreateInstance<CFMS30Splitter>,
		CFMS30Splitter::StaticInit,
		&sudFilterReg
	},
	{
		sudFilterRegSource.strName,
		sudFilterRegSource.clsID,
		CreateInstance<CFMS30SplitterSource>,
		nullptr,
		&sudFilterRegSource
	},
	// This entry is for the property page.
	//{
	//	L"LAV Splitter Properties",
	//	&CLSID_LAVSplitterSettingsProp,
	//	CreateInstance<CLAVSplitterSettingsProp>,
	//	nullptr, nullptr
	//}
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
	CUnknown *pInstance = CreateInstance<CFMS30Splitter>(nullptr, &hr);
	IBaseFilter *pFilter = nullptr;
	pInstance->NonDelegatingQueryInterface(IID_IBaseFilter, (void **)&pFilter);
	if (pFilter) {
		pFilter->AddRef();
		//CBaseDSPropPage::ShowPropPageDialog(pFilter);
	}
	delete pInstance;
}