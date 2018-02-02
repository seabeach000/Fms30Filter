// stdafx.h : ��׼ϵͳ�����ļ��İ����ļ���
// ���Ǿ���ʹ�õ��������ĵ�
// �ض�����Ŀ�İ����ļ�
//

#pragma once

#include "targetver.h"

#define WIN32_LEAN_AND_MEAN             // �� Windows ͷ���ų�����ʹ�õ�����



// TODO:  �ڴ˴����ó�����Ҫ������ͷ�ļ�
#include <atlbase.h>
#include <atlconv.h>

#include "streams.h"

#pragma warning(push)
#pragma warning(disable:4244)
extern "C" {
#define __STDC_CONSTANT_MACROS
#include "libavformat/avformat.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"

}
#pragma warning(pop)

#define AVFORMAT_INTERNAL_H
typedef struct AVCodecTag {
	enum AVCodecID id;
	unsigned int tag;
} AVCodecTag;


#include "DShowUtil.h"
#include <MMReg.h>

#include <Shlwapi.h>