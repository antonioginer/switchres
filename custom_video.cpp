/**************************************************************

   custom_video.cpp - Custom video library

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2016 - Chris Kennedy, Antonio Giner

 **************************************************************/

// standard windows headers
#include <windows.h>

#include <stdio.h>

#include "custom_video.h"
#include "custom_video_ati.h"
#include "custom_video_adl.h"
#include "custom_video_pstrip.h"
#include "log.h"

extern bool ati_is_legacy(int vendor, int device);

custom_video::custom_video()
{
}

custom_video::~custom_video()
{
}

//============================================================
//  custom_video::make
//============================================================

custom_video *custom_video::make(char *device_name, char *device_id, int method, char *s_param)
{
	if (method == CUSTOM_VIDEO_TIMING_POWERSTRIP)
	{
		m_custom_video = new pstrip_timing(device_name, s_param);
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
				m_custom_video = new ati_timing(device_name, s_param);
				if (m_custom_video)
				{
					m_custom_method = CUSTOM_VIDEO_TIMING_ATI_LEGACY;
					return m_custom_video;
				}
			}
			else
			{
				m_custom_video = new adl_timing(device_name, s_param);
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

	return nullptr;
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

bool custom_video::set_timing(modeline *mode)
{
	return false;
}

//============================================================
//  custom_video::add_mode
//============================================================

bool custom_video::add_mode(modeline *mode)
{
	return false;
}

//============================================================
//  custom_video::delete_mode
//============================================================

bool custom_video::delete_mode(modeline *mode)
{
	return false;
}

//============================================================
//  custom_video::update_mode
//============================================================

bool custom_video::update_mode(modeline *mode)
{
	return false;
}
