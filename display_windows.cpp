/**************************************************************

   display_windows.cpp - Display manager for Windows

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2016 - Chris Kennedy, Antonio Giner

 **************************************************************/

#include <stdio.h>
#include "display_windows.h"

const auto log_verbose = printf;
const auto log_error = printf;

//============================================================
//  windows_display::init
//============================================================

bool windows_display::init(display_settings *ds)
{
	m_lock_unsupported_modes = ds->lock_unsupported_modes;

	DISPLAY_DEVICEA lpDisplayDevice[DISPLAY_MAX];
	int idev = 0;
	int found = -1;

	while (idev < DISPLAY_MAX)
	{
		memset(&lpDisplayDevice[idev], 0, sizeof(DISPLAY_DEVICEA));
		lpDisplayDevice[idev].cb = sizeof(DISPLAY_DEVICEA);

		if (EnumDisplayDevicesA(NULL, idev, &lpDisplayDevice[idev], 0) == FALSE)
			break;

		if ((!strcmp(ds->screen, "auto") && (lpDisplayDevice[idev].StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE))
			|| !strcmp(ds->screen, lpDisplayDevice[idev].DeviceName))
			found = idev;

		idev++;
	}
	if (found != -1)
	{
		strncpy(m_device_name, lpDisplayDevice[found].DeviceName, sizeof(m_device_name));
		strncpy(m_device_id, lpDisplayDevice[found].DeviceID, sizeof(m_device_id));
		log_verbose("Switchres: %s: %s (%s)\n", m_device_name, lpDisplayDevice[found].DeviceString, m_device_id);

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
		log_verbose("Switchres: Failed obtaining default video registry key\n");
		return false;
	}

	log_verbose("Switchres: Device key: %s\n", m_device_key);
	
	// Initialize custom video
	modeline user_mode;
	memset(&user_mode, 0, sizeof(modeline));
	factory = new custom_video();
	video = factory->make(m_device_name, m_device_id, &user_mode, 0, m_device_key);
	if (video) video->init();

	// Build our display's mode list
	video_modes.clear();
	get_desktop_mode();
	get_available_video_modes();

	return true;
}


//============================================================
//  windows_display::get_desktop_mode
//============================================================

bool windows_display::get_desktop_mode()
{
	memset(&m_devmode, 0, sizeof(DEVMODEA));
	m_devmode.dmSize = sizeof(DEVMODEA);

	if (EnumDisplaySettingsExA(!strcmp(m_device_name, "auto")?NULL:m_device_name, ENUM_CURRENT_SETTINGS, &m_devmode, 0))
	{
		desktop_mode.width = m_devmode.dmDisplayOrientation == DMDO_DEFAULT || m_devmode.dmDisplayOrientation == DMDO_180? m_devmode.dmPelsWidth:m_devmode.dmPelsHeight;
		desktop_mode.height = m_devmode.dmDisplayOrientation == DMDO_DEFAULT || m_devmode.dmDisplayOrientation == DMDO_180? m_devmode.dmPelsHeight:m_devmode.dmPelsWidth;
		desktop_mode.refresh = m_devmode.dmDisplayFrequency;
		desktop_mode.interlace = (m_devmode.dmDisplayFlags & DM_INTERLACED)?1:0;
		return true;
	}
	return false;
}

//============================================================
//  windows_display::set_desktop_mode
//============================================================

bool windows_display::set_desktop_mode(modeline *mode, int flags)
{
	modeline *backup_mode = video->get_backup_mode();
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
//  windows_display::restore_desktop_mode
//============================================================

bool windows_display::restore_desktop_mode()
{
	if (ChangeDisplaySettingsExA(m_device_name, &m_devmode, NULL, 0, 0) == DISP_CHANGE_SUCCESSFUL)
		return true;

	return false;
}

//============================================================
//  windows_display::get_available_video_modes
//============================================================

int windows_display::get_available_video_modes()
{
	int iModeNum = 0, i = 0, j = 0, k = 1;
	DEVMODEA lpDevMode;

	memset(&lpDevMode, 0, sizeof(DEVMODEA));
	lpDevMode.dmSize = sizeof(DEVMODEA);

	log_verbose("Switchres: Searching for custom video modes...\n");

	while (EnumDisplaySettingsExA(m_device_name, iModeNum, &lpDevMode, m_lock_unsupported_modes?0:EDS_RAWMODE) != 0)
	{
		if (lpDevMode.dmBitsPerPel == 32 && lpDevMode.dmDisplayFixedOutput == DMDFO_DEFAULT)
		{
			modeline m;
			memset(&m, 0, sizeof(struct modeline));
			m.interlace = (lpDevMode.dmDisplayFlags & DM_INTERLACED)?1:0;
			m.width = lpDevMode.dmDisplayOrientation == DMDO_DEFAULT || lpDevMode.dmDisplayOrientation == DMDO_180? lpDevMode.dmPelsWidth:lpDevMode.dmPelsHeight;
			m.height = lpDevMode.dmDisplayOrientation == DMDO_DEFAULT || lpDevMode.dmDisplayOrientation == DMDO_180? lpDevMode.dmPelsHeight:lpDevMode.dmPelsWidth;
			m.refresh = lpDevMode.dmDisplayFrequency;
			m.hactive = m.width;
			m.vactive = m.height;
			m.vfreq = m.refresh;
			m.type |= lpDevMode.dmDisplayOrientation == DMDO_90 || lpDevMode.dmDisplayOrientation == DMDO_270? MODE_ROTATED : MODE_OK;

			for (auto &mode : video_modes) if (mode.width == m.width && mode.height == m.height && mode.refresh == m.refresh) goto found;

			if (m.width == desktop_mode.width && m.height == desktop_mode.height && m.refresh == desktop_mode.refresh)
			{
				m.type |= MODE_DESKTOP;
				if (m.type & MODE_ROTATED) m_desktop_rotated = true;
			}

			log_verbose("Switchres: [%3d] %4dx%4d @%3d%s %s: ", k, m.width, m.height, m.refresh, m.type & MODE_DESKTOP?"*":"",  m.type & MODE_ROTATED?"rot":"");

			if (video && video->get_timing(&m))
			{
				j++;
				if (m.type & MODE_DESKTOP) memcpy(&desktop_mode, &m, sizeof(modeline));

				char modeline_txt[256];
				log_verbose("%s timing %s\n", video->api_name(), modeline_print(&m, modeline_txt, MS_FULL));
			}
			else
			{
				m.type |= CUSTOM_VIDEO_TIMING_SYSTEM;
				log_verbose("system mode\n");
			}

			video_modes.push_back(m);
			k++;
		}
		found:
		iModeNum++;
	}
	k--;
	log_verbose("Switchres: Found %d custom of %d active video modes\n", j, k);
	return k;
}

