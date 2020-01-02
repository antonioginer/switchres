/**************************************************************

   custom_video_pstrip.cpp - PowerStrip interface routines

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2016 - Chris Kennedy, Antonio Giner

 **************************************************************/

/*	http://forums.entechtaiwan.com/index.php?topic=5534.msg20902;topicseen#msg20902

	UM_SETCUSTOMTIMING = WM_USER+200;
	wparam = monitor number, zero-based
	lparam = atom for string pointer
	lresult = -1 for failure else current pixel clock (integer in Hz)
	Note: pass full PowerStrip timing string*

	UM_SETREFRESHRATE = WM_USER+201;
	wparam = monitor number, zero-based
	lparam = refresh rate (integer in Hz), or 0 for read-only
	lresult = -1 for failure else current refresh rate (integer in Hz)

	UM_SETPOLARITY = WM_USER+202;
	wparam = monitor number, zero-based
	lparam = polarity bits
	lresult = -1 for failure else current polarity bits+1

	UM_REMOTECONTROL = WM_USER+210;
	wparam = 99
	lparam =
		0 to hide tray icon
		1 to show tray icon,
		2 to get build number
	   10 to show Performance profiles
	   11 to show Color profiles
	   12 to show Display profiles
	   13 to show Application profiles
	   14 to show Adapter information
	   15 to show Monitor information
	   16 to show Hotkey manager
	   17 to show Resource manager
	   18 to show Preferences
	   19 to show Online services
	   20 to show About screen
	   21 to show Tip-of-the-day
	   22 to show Setup wizard
	   23 to show Screen fonts
	   24 to show Advanced timing options
	   25 to show Custom resolutions
	   99 to close PS
	lresult = -1 for failure else lparam+1 for success or build number (e.g., 335)
	if lparam was 2

	UM_SETGAMMARAMP = WM_USER+203;
	wparam = monitor number, zero-based
	lparam = atom for string pointer
	lresult = -1 for failure, 1 for success

	UM_CREATERESOLUTION = WM_USER+204;
	wparam = monitor number, zero-based
	lparam = atom for string pointer
	lresult = -1 for failure, 1 for success
	Note: pass full PowerStrip timing string*; reboot is usually necessary to see if
	the resolution is accepted by the display driver

	UM_GETTIMING = WM_USER+205;
	wparam = monitor number, zero-based
	lresult = -1 for failure else GlobalAtom number identifiying the timing string*
	Note: be sure to call GlobalDeleteAtom after reading the string associated with
	the atom

	UM_GETSETCLOCKS = WM_USER+206;
	wparam = monitor number, zero-based
	lparam = atom for string pointer
	lresult = -1 for failure else GlobalAtom number identifiying the performance
	string**
	Note: pass full PowerStrip performance string** to set the clocks, and ull to
	get clocks; be sure to call GlobalDeleteAtom after reading the string associated
	with the atom

	NegativeHorizontalPolarity = 0x02;
	NegativeVerticalPolarity = 0x04;

	*Timing string parameter definition:
	 1 = horizontal active pixels
	 2 = horizontal front porch
	 3 = horizontal sync width
	 4 = horizontal back porch
	 5 = vertical active pixels
	 6 = vertical front porch
	 7 = vertical sync width
	 8 = vertical back porch
	 9 = pixel clock in hertz
	10 = timing flags, where bit:
		 1 = negative horizontal porlarity
		 2 = negative vertical polarity
		 3 = interlaced
		 5 = composite sync
		 7 = sync-on-green
		 all other bits reserved

	**Performance string parameter definition:
	 1 = memory clock in hertz
	 2 = engine clock in hertz
	 3 = reserved
	 4 = reserved
	 5 = reserved
	 6 = reserved
	 7 = reserved
	 8 = reserved
	 9 = 2D memory clock in hertz (if different from 3D)
	10 = 2D engine clock in hertz (if different from 3D) */

// standard windows headers
#include <windows.h>
#include <stdio.h>

// PowerStrip header
#include "custom_video_pstrip.h"

const auto log_verbose = printf;
const auto log_info = printf;
const auto log_error = printf;

//============================================================
//  GLOBALS
//============================================================

static HWND hPSWnd;
static MonitorTiming timing_backup;

//============================================================
//  CONSTANTS
//============================================================

#define UM_SETCUSTOMTIMING      (WM_USER+200)
#define UM_SETREFRESHRATE       (WM_USER+201)
#define UM_SETPOLARITY          (WM_USER+202)
#define UM_REMOTECONTROL        (WM_USER+210)
#define UM_SETGAMMARAMP         (WM_USER+203)
#define UM_CREATERESOLUTION     (WM_USER+204)
#define UM_GETTIMING            (WM_USER+205)
#define UM_GETSETCLOCKS         (WM_USER+206)
#define UM_SETCUSTOMTIMINGFAST  (WM_USER+211) // glitches vertical sync with PS 3.65 build 568

#define NegativeHorizontalPolarity      0x02
#define NegativeVerticalPolarity        0x04
#define Interlace                       0x08

#define HideTrayIcon                    0x00
#define ShowTrayIcon                    0x01
#define ClosePowerStrip                 0x63

//============================================================
//  ps_init
//============================================================

int ps_init(int monitor_index, modeline *modeline)
{
	hPSWnd = FindWindowA("TPShidden", NULL);

	if (hPSWnd)
	{
		log_verbose("PStrip: PowerStrip found!\n");
		if (ps_get_monitor_timing(monitor_index, &timing_backup) && modeline)
		{
			ps_pstiming_to_modeline(&timing_backup, modeline);
			return 1;
		}
	}
	else
		log_verbose("PStrip: Could not get PowerStrip API interface\n");

	return 0;
}

//============================================================
//  ps_reset
//============================================================

int ps_reset(int monitor_index)
{
	return ps_set_monitor_timing(monitor_index, &timing_backup);
}

//============================================================
//  ps_get_modeline
//============================================================

int ps_get_modeline(int monitor_index, modeline *modeline)
{
	MonitorTiming timing = {0};

	if (ps_get_monitor_timing(monitor_index, &timing))
	{
		ps_pstiming_to_modeline(&timing, modeline);
		return 1;
	}
	else return 0;
}

//============================================================
//  ps_set_modeline
//============================================================

int ps_set_modeline(int monitor_index, modeline *modeline)
{
	MonitorTiming timing = {0};

	ps_modeline_to_pstiming(modeline, &timing);

	timing.PixelClockInKiloHertz = ps_best_pclock(monitor_index, &timing, timing.PixelClockInKiloHertz);

	if (ps_set_monitor_timing(monitor_index, &timing))
		return 1;
	else
		return 0;
}

//============================================================
//  ps_get_monitor_timing
//============================================================

int ps_get_monitor_timing(int monitor_index, MonitorTiming *timing)
{
	LRESULT lresult;
	char in[256];

	if (!hPSWnd) return 0;

	lresult = SendMessage(hPSWnd, UM_GETTIMING, monitor_index, 0);

	if (lresult == -1)
	{
		log_verbose("PStrip: Could not get PowerStrip timing string\n");
		return 0;
	}

	if (!GlobalGetAtomNameA(lresult, in, sizeof(in)))
	{
		log_verbose("PStrip: GlobalGetAtomName failed\n");
		return 0;
	}

	log_verbose("PStrip: ps_get_monitor_timing(%d): %s\n", monitor_index, in);

	ps_read_timing_string(in, timing);

	GlobalDeleteAtom(lresult); // delete atom created by PowerStrip

	return 1;
}

//============================================================
//  ps_set_monitor_timing
//============================================================

int ps_set_monitor_timing(int monitor_index, MonitorTiming *timing)
{
	LRESULT lresult;
	ATOM atom;
	char out[256];

	if (!hPSWnd) return 0;

	ps_fill_timing_string(out, timing);
	atom = GlobalAddAtomA(out);

	if (atom)
	{
		lresult = SendMessage(hPSWnd, UM_SETCUSTOMTIMING, monitor_index, atom);

		if (lresult < 0)
		{
			log_verbose("PStrip: SendMessage failed\n");
			GlobalDeleteAtom(atom);
		}
		else
		{
			log_verbose("PStrip: ps_set_monitor_timing(%d): %s\n", monitor_index, out);
			return 1;
		}
	}
	else log_verbose("PStrip: ps_set_monitor_timing atom creation failed\n");

	return 0;
}

//============================================================
//  ps_set_monitor_timing_string
//============================================================

int ps_set_monitor_timing_string(int monitor_index, char *in)
{
	MonitorTiming timing;

	ps_read_timing_string(in, &timing);
	return ps_set_monitor_timing(monitor_index, &timing);
}

//============================================================
//  ps_set_refresh
//============================================================

int ps_set_refresh(int monitor_index, double vfreq)
{
	MonitorTiming timing = {0};
	int hht, vvt, new_vvt;
	int desired_pClock;
	int best_pClock;

	memcpy(&timing, &timing_backup, sizeof(MonitorTiming));

	hht = timing.HorizontalActivePixels
		+ timing.HorizontalFrontPorch
		+ timing.HorizontalSyncWidth
		+ timing.HorizontalBackPorch;

	vvt = timing.VerticalActivePixels
		+ timing.VerticalFrontPorch
		+ timing.VerticalSyncWidth
		+ timing.VerticalBackPorch;

	desired_pClock = hht * vvt * vfreq / 1000;
	best_pClock = ps_best_pclock(monitor_index, &timing, desired_pClock);

	new_vvt = best_pClock * 1000 / (vfreq * hht);

	timing.VerticalBackPorch += (new_vvt - vvt);
	timing.PixelClockInKiloHertz = best_pClock;

	ps_set_monitor_timing(monitor_index, &timing);
	ps_get_monitor_timing(monitor_index, &timing);

	return 1;
}

//============================================================
//  ps_best_pclock
//============================================================

int ps_best_pclock(int monitor_index, MonitorTiming *timing, int desired_pclock)
{
	MonitorTiming timing_read;
	int best_pclock = 0;

	log_verbose("PStrip: ps_best_pclock(%d), getting stable dotclocks for %d...\n", monitor_index, desired_pclock);

	for (int i = -50; i <= 50; i += 25)
	{
		timing->PixelClockInKiloHertz = desired_pclock + i;

		ps_set_monitor_timing(monitor_index, timing);
		ps_get_monitor_timing(monitor_index, &timing_read);

		if (abs(timing_read.PixelClockInKiloHertz - desired_pclock) < abs(desired_pclock - best_pclock))
			best_pclock = timing_read.PixelClockInKiloHertz;
	}

	log_verbose("PStrip: ps_best_pclock(%d), new dotclock: %d\n", monitor_index, best_pclock);

	return best_pclock;
}

//============================================================
//  ps_create_resolution
//============================================================

int ps_create_resolution(int monitor_index, modeline *modeline)
{
	LRESULT     lresult;
	ATOM        atom;
	char        out[256];
	MonitorTiming timing = {0};

	if (!hPSWnd) return 0;

	ps_modeline_to_pstiming(modeline, &timing);

	ps_fill_timing_string(out, &timing);
	atom = GlobalAddAtomA(out);

	if (atom)
	{
		lresult = SendMessage(hPSWnd, UM_CREATERESOLUTION, monitor_index, atom);

		if (lresult < 0)
        	{
        		log_verbose("PStrip: SendMessage failed\n");
        		GlobalDeleteAtom(atom);
        	}
        	else
        	{
        		log_verbose("PStrip: ps_create_resolution(%d): %dx%d succeded \n",
        			modeline->width, modeline->height, monitor_index);
        		return 1;
        	}
        }
        else log_verbose("PStrip: ps_create_resolution atom creation failed\n");

	return 0;
}

//============================================================
//  ps_read_timing_string
//============================================================

bool ps_read_timing_string(char *in, MonitorTiming *timing)
{
	if (sscanf(in,"%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
		&timing->HorizontalActivePixels,
		&timing->HorizontalFrontPorch,
		&timing->HorizontalSyncWidth,
		&timing->HorizontalBackPorch,
		&timing->VerticalActivePixels,
		&timing->VerticalFrontPorch,
		&timing->VerticalSyncWidth,
		&timing->VerticalBackPorch,
		&timing->PixelClockInKiloHertz,
		&timing->TimingFlags.w) == 10) return true;

	return false;
}

//============================================================
//  ps_fill_timing_string
//============================================================

void ps_fill_timing_string(char *out, MonitorTiming *timing)
{
	sprintf(out, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
		timing->HorizontalActivePixels,
		timing->HorizontalFrontPorch,
		timing->HorizontalSyncWidth,
		timing->HorizontalBackPorch,
		timing->VerticalActivePixels,
		timing->VerticalFrontPorch,
		timing->VerticalSyncWidth,
		timing->VerticalBackPorch,
		timing->PixelClockInKiloHertz,
		timing->TimingFlags.w);
}

//============================================================
//  ps_modeline_to_pstiming
//============================================================

int ps_modeline_to_pstiming(modeline *modeline, MonitorTiming *timing)
{
	timing->HorizontalActivePixels = modeline->hactive;
	timing->HorizontalFrontPorch = modeline->hbegin - modeline->hactive;
	timing->HorizontalSyncWidth = modeline->hend - modeline->hbegin;
	timing->HorizontalBackPorch = modeline->htotal - modeline->hend;

	timing->VerticalActivePixels = modeline->vactive;
	timing->VerticalFrontPorch = modeline->vbegin - modeline->vactive;
	timing->VerticalSyncWidth = modeline->vend - modeline->vbegin;
	timing->VerticalBackPorch = modeline->vtotal - modeline->vend;

	timing->PixelClockInKiloHertz = modeline->pclock / 1000;

	if (modeline->hsync == 0)
		timing->TimingFlags.w |= NegativeHorizontalPolarity;
	if (modeline->vsync == 0)
		timing->TimingFlags.w |= NegativeVerticalPolarity;
	if (modeline->interlace)
		timing->TimingFlags.w |= Interlace;

	return 0;
}

//============================================================
//  ps_pstiming_to_modeline
//============================================================

int ps_pstiming_to_modeline(MonitorTiming *timing, modeline *modeline)
{
	modeline->hactive = timing->HorizontalActivePixels;
	modeline->hbegin = modeline->hactive + timing->HorizontalFrontPorch;
	modeline->hend = modeline->hbegin + timing->HorizontalSyncWidth;
	modeline->htotal = modeline->hend + timing->HorizontalBackPorch;

	modeline->vactive = timing->VerticalActivePixels;
	modeline->vbegin = modeline->vactive + timing->VerticalFrontPorch;
	modeline->vend = modeline->vbegin + timing->VerticalSyncWidth;
	modeline->vtotal = modeline->vend + timing->VerticalBackPorch;

	modeline->width = modeline->hactive;
	modeline->height = modeline->vactive;

	modeline->pclock = timing->PixelClockInKiloHertz * 1000;

	if (!(timing->TimingFlags.w & NegativeHorizontalPolarity))
		modeline->hsync = 1;

	if (!(timing->TimingFlags.w & NegativeVerticalPolarity))
		modeline->vsync = 1;

	if ((timing->TimingFlags.w & Interlace))
		modeline->interlace = 1;

	modeline->hfreq = modeline->pclock / modeline->htotal;
	modeline->vfreq = modeline->hfreq / modeline->vtotal * (modeline->interlace?2:1);

	return 0;
}

//============================================================
//  ps_monitor_index
//============================================================

int ps_monitor_index (const char *display_name)
{
	int monitor_index = 0;
	char sub_index[2];

	sub_index[0] = display_name[strlen(display_name)-1];
	sub_index[1] = 0;
	if (sscanf(sub_index,"%d", &monitor_index) == 1)
		monitor_index --;

	return monitor_index;
}
