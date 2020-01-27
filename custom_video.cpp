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

custom_video *custom_video::make(char *device_name, char *device_id, modeline *user_mode, int method, char *s_param)
{
	if (method == CUSTOM_VIDEO_TIMING_POWERSTRIP)
	{
		m_custom_video = new pstrip_timing(device_name, user_mode, s_param);
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
//  custom_video::close
//============================================================

void custom_video::close() {}
/*
void custom_video::close()
{
	switch (custom_method)
	{
		case CUSTOM_VIDEO_TIMING_ATI_LEGACY:
			break;
		
		case CUSTOM_VIDEO_TIMING_ATI_ADL:
			adl_close();
			break;

		case CUSTOM_VIDEO_TIMING_POWERSTRIP:
			break;
	}
}
*/

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
/*
//============================================================
//  custom_video::restore_timing
//============================================================

bool custom_video::restore_timing()
{
	if (!m_backup_mode.hactive)
		return false;

	// Restore backup mode
	return update_timing(0);
}

//============================================================
//  custom_video_refresh_timing
//============================================================

void custom_video::refresh_timing()
{
	switch (custom_method)
	{
		case CUSTOM_VIDEO_TIMING_ATI_LEGACY:
//			ati_refresh_timings();
			break;
		
		case CUSTOM_VIDEO_TIMING_ATI_ADL:
			break;

		case CUSTOM_VIDEO_TIMING_POWERSTRIP:
			break;
	}
}

//============================================================
//  custom_video_update_timing
//============================================================

bool custom_video::update_mode(modeline *mode)
{
	switch (custom_method)
	{
		case CUSTOM_VIDEO_TIMING_ATI_LEGACY:
		case CUSTOM_VIDEO_TIMING_ATI_ADL:

			// Restore old video timing
			if (m_backup_mode.hactive)
			{
				log_verbose("Switchres: restoring ");
				set_timing(&m_backup_mode);
			}

			// Update with new video timing
			if (mode)
			{
				// Backup current timing
				int found = 0;
				for (int i = 0; i <= MAX_MODELINES; i++)
				{
					if (m_mode_table[i].width == mode->width && m_mode_table[i].height == mode->height && m_mode_table[i].refresh == mode->refresh)
					{
						memcpy(&m_backup_mode, &m_mode_table[i], sizeof(modeline));
						found = 1;
						break;
					}
				}
				if (!found)
				{
					log_verbose("Switchres: mode not found in mode_table\n");
					return false;
				}
				log_verbose("Switchres: saving    ");
				get_timing(&m_backup_mode);

				// Apply new timing now
				log_verbose("Switchres: updating  ");
				if (!set_timing(mode)) goto error;
			}
			refresh_timing();
			break;

		case CUSTOM_VIDEO_TIMING_POWERSTRIP:
			// We only backup/restore the desktop mode with Powerstrip
			if (!mode)
				ps_reset(ps_monitor_index(m_device_name));
			else
			{
				log_verbose("Switchres: updating  ");
				set_timing(mode);
			}
			break;
	}
	return true;

error:
	log_verbose(": error updating video timings\n");
	return false;
}
*/

//============================================================
//  custom_video::update_mode
//============================================================

bool custom_video::add_mode(modeline *mode)
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
