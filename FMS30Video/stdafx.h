// stdafx.h : ��׼ϵͳ�����ļ��İ����ļ���
// ���Ǿ���ʹ�õ��������ĵ�
// �ض�����Ŀ�İ����ļ�
//

#pragma once

#include "targetver.h"

// Support for Version 6.0 styles
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include "common_defines.h"

#define WIN32_LEAN_AND_MEAN             // �� Windows ͷ���ų�����ʹ�õ�����
// Windows ͷ�ļ�: 
#include <Windows.h>
#include <Commctrl.h>

#include <atlbase.h>
#include <atlconv.h>

// TODO:  �ڴ˴����ó�����Ҫ������ͷ�ļ�

#include <d3d9.h>
#include <dxva2api.h>
#include <dvdmedia.h>


#pragma warning(push)
#pragma warning(disable:4244)
extern "C" {
#define __STDC_CONSTANT_MACROS
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/avfiltergraph.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
}
#pragma warning(pop)

#include "streams.h"

#include "DShowUtil.h"
#include "growarray.h"

#include "SubRenderIntf.h"

#include <initguid.h>

#define REF_SECOND_MULT 10000000LL