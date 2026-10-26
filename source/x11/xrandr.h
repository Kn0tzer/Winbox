/*
 *  Copyright (C) 2012-2025  The BoxedWine Team
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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __XRANDR_H__
#define __XRANDR_H__

/* Event selection bits */
#define RRScreenChangeNotifyMask  (1L << 0)
/* V1.2 additions */
#define RRCrtcChangeNotifyMask	    (1L << 1)
#define RROutputChangeNotifyMask    (1L << 2)
#define RROutputPropertyNotifyMask  (1L << 3)
/* V1.4 additions */
#define RRProviderChangeNotifyMask   (1L << 4)
#define RRProviderPropertyNotifyMask (1L << 5)
#define RRResourceChangeNotifyMask   (1L << 6)
/* V1.6 additions */
#define RRLeaseNotifyMask            (1L << 7)

/* Event codes */
#define RRScreenChangeNotify	0
/* V1.2 additions */
#define RRNotify		    1
/* RRNotify Subcodes */
#define  RRNotify_CrtcChange	    0
#define  RRNotify_OutputChange	    1
#define  RRNotify_OutputProperty    2
#define  RRNotify_ProviderChange    3
#define  RRNotify_ProviderProperty  4
#define  RRNotify_ResourceChange    5
/* V1.6 additions */
#define  RRNotify_Lease             6
/* used in the rotation field; rotation and reflection in 0.1 proto. */
#define RR_Rotate_0		1
#define RR_Rotate_90		2
#define RR_Rotate_180		4
#define RR_Rotate_270		8

typedef XID RROutput;
typedef XID RRCrtc;
typedef XID RRMode;
typedef XID RRProvider;

struct XRRScreenSize {
    S32	width, height;
    S32	mwidth, mheight;
};

static_assert(sizeof(XRRScreenSize) == 16, "emulation expects sizeof(XRRScreenSize) to be 16");

typedef U32 XRRModeFlags;

struct XRRModeInfo {
    RRMode		id;
    U32	width;
    U32	height;
    U32	dotClock;
    U32	hSyncStart;
    U32	hSyncEnd;
    U32	hTotal;
    U32	hSkew;
    U32	vSyncStart;
    U32	vSyncEnd;
    U32	vTotal;
    U32 name; // char*
    U32	nameLength;
    XRRModeFlags	modeFlags;
};

struct XRRScreenResources {
    Time	timestamp;
    Time	configTimestamp;
    S32		ncrtc;
    U32     crtcs; // RRCrtc*
    S32		noutput;
    U32     outputs; // RROutput*
    S32		nmode;
    U32     modes; // XRRModeInfo*
};

// 32-bit guest ABI layouts from Xrandr.h. Pointer members remain guest addresses.
struct XRRProviderResources {
    Time timestamp;
    S32 nproviders;
    U32 providers;
};

struct XRRCrtcInfo {
    Time timestamp;
    S32 x, y;
    U32 width, height;
    RRMode mode;
    U32 rotation;
    S32 noutput;
    U32 outputs;
    U32 rotations;
    S32 npossible;
    U32 possible;
};

struct XRROutputInfo {
    Time timestamp;
    RRCrtc crtc;
    U32 name;
    S32 nameLen;
    U32 mm_width, mm_height;
    U32 connection;
    U32 subpixel_order;
    S32 ncrtc;
    U32 crtcs;
    S32 nclone;
    U32 clones;
    S32 nmode;
    S32 npreferred;
    U32 modes;
};

static_assert(sizeof(XRRModeInfo) == 56, "emulation expects the 32-bit XRRModeInfo ABI");
static_assert(sizeof(XRRScreenResources) == 32, "emulation expects the 32-bit XRRScreenResources ABI");
static_assert(sizeof(XRRCrtcInfo) == 48, "emulation expects the 32-bit XRRCrtcInfo ABI");
static_assert(sizeof(XRROutputInfo) == 60, "emulation expects the 32-bit XRROutputInfo ABI");

class XrrData {
public:
    U32 sizesAddress = 0; // XRRScreenSize*
    U32 sizesCount = 0;
    U32 ratesAddress = 0;
    U32 eventWindow = 0;
    U32 eventMask = 0;
};

#define RRSetConfigSuccess		0
#define RRSetConfigInvalidConfigTime	1
#define RRSetConfigInvalidTime		2
#define RRSetConfigFailed		3

U32 XrrGetSizes(KThread* thread, const DisplayDataPtr& data, U32 screen, U32 countAddress);
U32 XrrConfigCurrentConfiguration(KThread* thread, const DisplayDataPtr& data, U32 rotationAddress);
U32 XrrConfigCurrentRate();
U32 XrrRates(KThread* thread, const DisplayDataPtr& data, U32 screen, U32 sizeIndex, U32 rateCountAddress);
bool XrrGetSize(KThread* thread, const DisplayDataPtr& displayData, U32 sizeIndex, U32& cx, U32& cy);

#endif