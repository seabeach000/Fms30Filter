// stdafx.h : 标准系统包含文件的包含文件，
// 或是经常使用但不常更改的
// 特定于项目的包含文件
//

#pragma once

#include "targetver.h"

#define WIN32_LEAN_AND_MEAN             // 从 Windows 头中排除极少使用的资料
// Windows 头文件: 
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
// TODO:  在此处引用程序需要的其他头文件
