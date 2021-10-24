/**************************************************************

   custom_video.cpp - Custom video library

   ---------------------------------------------------------

   Switchres   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2021 Chris Kennedy, Antonio Giner,
                         Alexandre Wodarczyk, Gil Delescluse

 **************************************************************/



#include <stdio.h>
#include "custom_video.h"
#include "log.h"

#if defined(_WIN32)
#include "custom_video_ati.h"
#include "custom_video_adl.h"
#include "custom_video_pstrip.h"
#elif defined(__linux__)
#ifdef SR_WITH_XRANDR
#include "custom_video_xrandr.h"
#endif
#ifdef SR_WITH_KMSDRM
#include "custom_video_drmkms.h"
#endif
#endif


extern bool ati_is_legacy(int vendor, int device);

//============================================================
//  custom_video::make
//============================================================

custom_video *custom_video::make(char *device_name, char *device_id, int method, custom_video_settings *vs)
{
#if defined(_WIN32)
	if (method == CUSTOM_VIDEO_TIMING_POWERSTRIP)
	{
		m_custom_video = new pstrip_timing(device_name, vs);
		if (m_custom_video)
		{
			m_custom_method = CUSTOM_VIDEO_TIMING_POWERSTRIP;
			return m_custom_video;
		}
	}
	else
	{
		int vendor, device;
		sscanf(device_id, "PCI\\VEN_%x&DEV_%x", &vendor, &device);

		if (vendor == 0x1002) // ATI/AMD
		{
			if (ati_is_legacy(vendor, device))
			{
				m_custom_video = new ati_timing(device_name, vs);
				if (m_custom_video)
				{
					m_custom_method = CUSTOM_VIDEO_TIMING_ATI_LEGACY;
					return m_custom_video;
				}
			}
			else
			{
				m_custom_video = new adl_timing(device_name, vs);
				if (m_custom_video)
				{
					m_custom_method = CUSTOM_VIDEO_TIMING_ATI_ADL;
					return m_custom_video;
				}
			}
		}
		else
			log_info("Video chipset is not compatible.\n");
	}
#elif defined(__linux__)
	if (device_id != NULL)
		log_info("Device value is %s.\n", device_id);

#ifdef SR_WITH_XRANDR
	if (method == CUSTOM_VIDEO_TIMING_XRANDR || method == 0)
	{
		try
		{
			m_custom_video = new xrandr_timing(device_name, vs);
		}
		catch (...) {};
		if (m_custom_video)
		{
			m_custom_method = CUSTOM_VIDEO_TIMING_XRANDR;
			return m_custom_video;
		}
	}
#endif /* SR_WITH_XRANDR */

#ifdef SR_WITH_KMSDRM
	if (method == CUSTOM_VIDEO_TIMING_DRMKMS || method == 0)
	{
		m_custom_video = new drmkms_timing(device_name, vs);
		if (m_custom_video)
		{
			m_custom_method = CUSTOM_VIDEO_TIMING_DRMKMS;
			return m_custom_video;
		}
	}
#endif /* SR_WITH_KMSDRM */
#endif

	return this;
}

//============================================================
//  custom_video::init
//============================================================

bool custom_video::init() { return false; }

//============================================================
//  custom_video::get_timing
//============================================================

bool custom_video::get_timing(modeline *mode)
{
	log_verbose("system mode\n");
	mode->type |= CUSTOM_VIDEO_TIMING_SYSTEM;
	return false;
}

//============================================================
//  custom_video::set_timing
//============================================================

bool custom_video::set_timing(modeline *)
{
	return false;
}

//============================================================
//  custom_video::add_mode
//============================================================

bool custom_video::add_mode(modeline *)
{
	return false;
}

//============================================================
//  custom_video::delete_mode
//============================================================

bool custom_video::delete_mode(modeline *)
{
	return false;
}

//============================================================
//  custom_video::update_mode
//============================================================

bool custom_video::update_mode(modeline *)
{
	return false;
}

//============================================================
//  custom_video::process_modelist
//============================================================

bool custom_video::process_modelist(std::vector<modeline *>)
{
	return false;
}

#ifdef SR_WITH_SDL2
//============================================================
//  custom_video::get_sdl_hwinfo_from_sdl_window
//============================================================
void custom_video::get_sdl_hwinfo_from_sdl_window(SDL_Window* window)
{
	m_sdlwindow = window;
	SDL_VERSION(&m_sdlwminfo.version);
	if(! SDL_GetWindowWMInfo(window, &m_sdlwminfo))
	{
		log_error("Couldn't get the SDL WMInfo\n");
		return;
	}
	const char *subsystem = "an unsupported or unknown system!";
	switch(m_sdlwminfo.subsystem)
	{
		case SDL_SYSWM_UNKNOWN:
		case SDL_SYSWM_COCOA:
		case SDL_SYSWM_UIKIT:
		case SDL_SYSWM_WAYLAND:
		case SDL_SYSWM_MIR:
		case SDL_SYSWM_WINRT:
		case SDL_SYSWM_ANDROID:
		case SDL_SYSWM_VIVANTE:
		case SDL_SYSWM_OS2:
		case SDL_SYSWM_HAIKU:
		case SDL_SYSWM_DIRECTFB:
			break;
		case SDL_SYSWM_WINDOWS:
			subsystem = "Microsoft Windows(TM)";
			break;
		case SDL_SYSWM_X11:
			subsystem = "X Window System";
			break;
		case SDL_SYSWM_KMSDRM:
			subsystem = "KMSDRM";
			break;
	}
	log_info("Switchres: Detected SDL version %d.%d.%d on %s\n",
		(int)m_sdlwminfo.version.major,
		(int)m_sdlwminfo.version.minor,
		(int)m_sdlwminfo.version.patch,
		subsystem);
}
#endif