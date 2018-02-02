/*
 *      Copyright (C) 2010-2017 Hendrik Leppkes
 *      http://www.1f0.de
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#pragma once

#include <Unknwn.h>       // IUnknown and GUID Macros

// {774A919D-EA95-4A87-8A1E-F48ABE8499C7}
DEFINE_GUID(IID_ILAVFSettings, 
0xA77A2527, 0xB694, 0x4177, 0x8B, 0xB7, 0xC4, 0x63, 0x8F, 0x19, 0x71, 0x7D);

typedef enum LAVSubtitleMode {
  LAVSubtitleMode_NoSubs,
  LAVSubtitleMode_ForcedOnly,
  LAVSubtitleMode_Default,
  LAVSubtitleMode_Advanced
} LAVSubtitleMode;

interface __declspec(uuid("A77A2527-B694-4177-8BB7-C4638F19717D")) ILAVFSettings : public IUnknown
{

  // Set the maximum queue size, in megabytes
  STDMETHOD(SetMaxQueueMemSize)(DWORD dwMaxSize) = 0;

  // Get the maximum queue size, in megabytes
  STDMETHOD_(DWORD,GetMaxQueueMemSize)() = 0;

  // Set the duration (in ms) of analysis for network streams (to find the streams and codec parameters)
  STDMETHOD(SetNetworkStreamAnalysisDuration)(DWORD dwDuration) = 0;

  // Get the duration (in ms) of analysis for network streams (to find the streams and codec parameters)
  STDMETHOD_(DWORD, GetNetworkStreamAnalysisDuration)() = 0;

  // Set the maximum queue size, in number of packets
  STDMETHOD(SetMaxQueueSize)(DWORD dwMaxSize) = 0;

  // Get the maximum queue size, in number of packets
  STDMETHOD_(DWORD, GetMaxQueueSize)() = 0;
};
