// stdafx.h : ��׼ϵͳ�����ļ��İ����ļ���
// ���Ǿ���ʹ�õ��������ĵ�
// �ض�����Ŀ�İ����ļ�
//

#pragma once

#include "targetver.h"

#define WIN32_LEAN_AND_MEAN             // �� Windows ͷ���ų�����ʹ�õ�����
// Windows ͷ�ļ�: 
#include "common_defines.h"

// include headers
#include <Windows.h>
#include <Commctrl.h>

#include <atlbase.h>
#include <atlconv.h>

#pragma warning(push)
#pragma warning(disable:4244)
extern "C" {
#define __STDC_CONSTANT_MACROS
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/opt.h"
#include "libavutil/intreadwrite.h"
#include <libswresample/swresample.h>
}

#include "streams.h"

#include "DShowUtil.h"
#include "growarray.h"

#include <initguid.h>
// TODO:  �ڴ˴����ó�����Ҫ������ͷ�ļ�
