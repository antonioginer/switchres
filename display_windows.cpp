/**************************************************************

   display.cpp - Display manager

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2016 - Chris Kennedy, Antonio Giner

 **************************************************************/

#include <stdio.h>
#include <windows.h>
#include "modeline.h"
#include "custom_video.h"
#include "display.h"

const auto log_verbose = printf;
const auto log_error = printf;

//============================================================
//  PARAMETERS
//============================================================

// display modes
#define DM_INTERLACED 0x00000002
#define DISPLAY_MAX 16

//============================================================
//  LOCAL VARIABLES
//============================================================

static char m_device_name[32];
static char m_device_id[128];
static char m_device_key[128];
static DEVMODEA desktop_devmode;

display_manager::display_manager()
{
}

display_manager::~display_manager()
{
}

//============================================================
//  display_manager::init
//============================================================

int display_manager::init(const char *screen_option)
{
	DISPLAY_DEVICEA lpDisplayDevice[DISPLAY_MAX];
	int idev = 0;
	int found = -1;

	while (idev < DISPLAY_MAX)
	{
		memset(&lpDisplayDevice[idev], 0, sizeof(DISPLAY_DEVICEA));
		lpDisplayDevice[idev].cb = sizeof(DISPLAY_DEVICEA);

		if (EnumDisplayDevicesA(NULL, idev, &lpDisplayDevice[idev], 0) == FALSE)
			break;

		if ((!strcmp(screen_option, "auto") && (lpDisplayDevice[idev].StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE))
			|| !strcmp(screen_option, lpDisplayDevice[idev].DeviceName))
			found = idev;

		idev++;
	}
	if (found != -1)
	{
		strncpy(m_device_name, lpDisplayDevice[found].DeviceName, sizeof(m_device_name));
		strncpy(m_device_id, lpDisplayDevice[found].DeviceID, sizeof(m_device_id));
		log_verbose("SwitchRes: %s: %s (%s)\n", m_device_name, lpDisplayDevice[found].DeviceString, m_device_id);

		char *pch;
		int i;
		for (i = 0; i < idev; i++)
		{
			pch = strstr(lpDisplayDevice[i].DeviceString, lpDisplayDevice[found].DeviceString);
			if (pch)
			{
				found = i;
				break;
			}
		}

		char *chsrc, *chdst;
		chdst = m_device_key;

		for (chsrc = lpDisplayDevice[i].DeviceKey + 18; *chsrc != 0; chsrc++)
			*chdst++ = *chsrc;

		*chdst = 0;
	}
	else
	{
		log_verbose("SwitchRes: Failed obtaining default video registry key\n");
		return -1;
	}

	log_verbose("SwitchRes: Device key: %s\n", m_device_key);
	return 0;
}


//============================================================
//  display_manager::get_desktop_mode
//============================================================

int display_manager::get_desktop_mode()
{
	memset(&desktop_devmode, 0, sizeof(DEVMODEA));
	desktop_devmode.dmSize = sizeof(DEVMODEA);

	if (EnumDisplaySettingsExA(!strcmp(m_device_name, "auto")?NULL:m_device_name, ENUM_CURRENT_SETTINGS, &desktop_devmode, 0))
	{
		desktop_mode.width = desktop_devmode.dmDisplayOrientation == DMDO_DEFAULT || desktop_devmode.dmDisplayOrientation == DMDO_180? desktop_devmode.dmPelsWidth:desktop_devmode.dmPelsHeight;
		desktop_mode.height = desktop_devmode.dmDisplayOrientation == DMDO_DEFAULT || desktop_devmode.dmDisplayOrientation == DMDO_180? desktop_devmode.dmPelsHeight:desktop_devmode.dmPelsWidth;
		desktop_mode.refresh = desktop_devmode.dmDisplayFrequency;
		desktop_mode.interlace = (desktop_devmode.dmDisplayFlags & DM_INTERLACED)?1:0;
		return true;
	}
	return false;
}

//============================================================
//  display_manager::set_desktop_mode
//============================================================

int display_manager::set_desktop_mode(modeline *mode, int flags)
{
	//modeline *backup_mode = custom_video_get_backup_mode();
	modeline *backup_mode = nullptr;
	modeline *mode_to_check_interlace = backup_mode->hactive? backup_mode : mode;
	DEVMODEA lpDevMode;

	get_desktop_mode();

	if (mode)
	{
		memset(&lpDevMode, 0, sizeof(DEVMODEA));
		lpDevMode.dmSize = sizeof(DEVMODEA);
		lpDevMode.dmPelsWidth = mode->type & MODE_ROTATED? mode->height : mode->width;
		lpDevMode.dmPelsHeight = mode->type & MODE_ROTATED? mode->width : mode->height;
		lpDevMode.dmDisplayFrequency = (int)mode->refresh;
		lpDevMode.dmDisplayFlags = mode_to_check_interlace->interlace?DM_INTERLACED:0;
		lpDevMode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY | DM_DISPLAYFLAGS;

		if (ChangeDisplaySettingsExA(m_device_name, &lpDevMode, NULL, flags, 0) == DISP_CHANGE_SUCCESSFUL)
			return true;
	}

	return false;
}

//============================================================
//  display_manager::restore_desktop_mode
//============================================================

int display_manager::restore_desktop_mode()
{
	if (ChangeDisplaySettingsExA(m_device_name, &desktop_devmode, NULL, 0, 0) == DISP_CHANGE_SUCCESSFUL)
		return true;

	return false;
}

//============================================================
//  display_manager::get_available_video_modes
//============================================================

int display_manager::get_available_video_modes()
{
	int iModeNum = 0, i = 0, j = 0, k = 1;
	DEVMODEA lpDevMode;

	memset(&lpDevMode, 0, sizeof(DEVMODEA));
	lpDevMode.dmSize = sizeof(DEVMODEA);

	log_verbose("Switchres: Searching for custom video modes...\n");

	while (EnumDisplaySettingsExA(m_device_name, iModeNum, &lpDevMode, m_lock_unsupported_modes?0:EDS_RAWMODE) != 0)
	{
		if (k == MAX_MODELINES)
		{
			log_verbose("SwitchRes: Warning, too many active modelines for storage %d\n", k);
			break;
		}
		else if (lpDevMode.dmBitsPerPel == 32 && lpDevMode.dmDisplayFixedOutput == DMDFO_DEFAULT)
		{
			modeline *m = &video_modes[k];
			memset(m, 0, sizeof(struct modeline));
			m->interlace = (lpDevMode.dmDisplayFlags & DM_INTERLACED)?1:0;
			m->width = lpDevMode.dmDisplayOrientation == DMDO_DEFAULT || lpDevMode.dmDisplayOrientation == DMDO_180? lpDevMode.dmPelsWidth:lpDevMode.dmPelsHeight;
			m->height = lpDevMode.dmDisplayOrientation == DMDO_DEFAULT || lpDevMode.dmDisplayOrientation == DMDO_180? lpDevMode.dmPelsHeight:lpDevMode.dmPelsWidth;
			m->refresh = lpDevMode.dmDisplayFrequency;
			m->hactive = m->width;
			m->vactive = m->height;
			m->vfreq = m->refresh;
			m->type |= lpDevMode.dmDisplayOrientation == DMDO_90 || lpDevMode.dmDisplayOrientation == DMDO_270? MODE_ROTATED : MODE_OK;

			for (i = 0; i < k; i++) if (video_modes[i].width == m->width && video_modes[i].height == m->height && video_modes[i].refresh == m->refresh) goto found;

			if (m->width == desktop_mode.width && m->height == desktop_mode.height && m->refresh == desktop_mode.refresh)
			{
				m->type |= MODE_DESKTOP;
				if (m->type & MODE_ROTATED) m_desktop_rotated = true;
			}

			log_verbose("Switchres: [%3d] %4dx%4d @%3d%s %s: ", k, m->width, m->height, m->refresh, m->type & MODE_DESKTOP?"*":"",  m->type & MODE_ROTATED?"rot":"");

/*			if (custom_video_get_timing(m))
			{
				j++;
				if (m->type & MODE_DESKTOP) memcpy(desktop_mode, m, sizeof(modeline));
			}*/
			k++;
		}
		found:
		iModeNum++;
	}
	k--;
	log_verbose("SwitchRes: Found %d custom of %d active video modes\n", j, k);
	return k;
}
