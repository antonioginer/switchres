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

extern bool ati_is_legacy(int vendor, int device);

const auto log_verbose = printf;
const auto log_info = printf;
const auto log_error = printf;

custom_video::custom_video()
{
}

custom_video::~custom_video()
{
}


//============================================================
//  custom_video::init
//============================================================

bool custom_video::init(char *device_name, char *device_id, modeline *desktop_mode, modeline *user_mode, modeline *mode_table, int method, char *s_param)
{
	memset(&m_backup_mode, 0, sizeof(modeline));
	memcpy(&m_user_mode, user_mode, sizeof(modeline));
	memcpy(m_device_name, device_name, sizeof(m_device_name));
	m_mode_table = mode_table;

	if ((method == CUSTOM_VIDEO_TIMING_POWERSTRIP) && ps_init(ps_monitor_index(m_device_name), &m_backup_mode))
	{
		custom_method = CUSTOM_VIDEO_TIMING_POWERSTRIP;
		m_backup_mode.type |= CUSTOM_VIDEO_TIMING_POWERSTRIP;

		// If we have a -ps_timing string defined, use it as user defined modeline
		memcpy(ps_timing, s_param, sizeof(ps_timing));
		if (strcmp(ps_timing, "auto"))
		{
			MonitorTiming timing;
			if (ps_read_timing_string(ps_timing, &timing))
			{
				ps_pstiming_to_modeline(&timing, &m_user_mode);
				m_user_mode.type |= CUSTOM_VIDEO_TIMING_POWERSTRIP;
				memcpy(user_mode, &m_user_mode, sizeof(modeline));

				char modeline_txt[256]={'\x00'};
				log_verbose("SwitchRes: ps_string: %s (%s)\n", ps_timing, modeline_print(&m_user_mode, modeline_txt, MS_PARAMS));
			}
			else log_verbose("Switchres: ps_timing string with invalid format\n");
		}
		return true;
	}
	else
	{
		int vendor, device;
		parse_pci_id(device_id, &vendor, &device);

		if (vendor == 0x1002) // ATI/AMD
		{
			if (ati_is_legacy(vendor, device))
			{
				memcpy(m_device_key, s_param, sizeof(m_device_key));
				if (ati_init(m_device_name, m_device_key, device_id))
				{
					custom_method = CUSTOM_VIDEO_TIMING_ATI_LEGACY;
					return true;
				}
			}
			else
			{
				memcpy(m_device_key, s_param, sizeof(m_device_key));
				if (adl_init(m_device_name, m_device_key, device_id))
				{
					custom_method = CUSTOM_VIDEO_TIMING_ATI_ADL;
					return true;
				}
			}
		}
		else
			log_info("Video chipset is not compatible.\n");
	}

	return false;
}

//============================================================
//  custom_video::close
//============================================================

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

//============================================================
//  custom_video::get_timing
//============================================================

bool custom_video::get_timing(modeline *mode)
{
	char modeline_txt[256]={'\x00'};

	switch (custom_method)
	{
		case CUSTOM_VIDEO_TIMING_ATI_LEGACY:
			if (ati_get_modeline(mode))
			{
				log_verbose("ATI legacy timing %s\n", modeline_print(mode, modeline_txt, MS_FULL));
				mode->type |= CUSTOM_VIDEO_TIMING_ATI_LEGACY | (!(mode->type & MODE_DESKTOP)? V_FREQ_EDITABLE | (mode->width == DUMMY_WIDTH? X_RES_EDITABLE:0):0);
				return true;
			}
			break;
		
		case CUSTOM_VIDEO_TIMING_ATI_ADL:
			if (adl_get_modeline(m_device_name, mode))
			{
				log_verbose("ATI ADL timing %s\n", modeline_print(mode, modeline_txt, MS_FULL));
				mode->type |= CUSTOM_VIDEO_TIMING_ATI_ADL | (!(mode->type & MODE_DESKTOP)? V_FREQ_EDITABLE :0);
				return true;
			}
			break;

		case CUSTOM_VIDEO_TIMING_POWERSTRIP:
			if ((mode->type & MODE_DESKTOP) && ps_get_modeline(ps_monitor_index(m_device_name), mode))
				log_verbose("Powerstrip timing %s\n", modeline_print(mode, modeline_txt, MS_FULL));
			else
				log_verbose("Not current mode\n");

			mode->type |= CUSTOM_VIDEO_TIMING_POWERSTRIP | V_FREQ_EDITABLE;
			return true;
	}
	
	log_verbose("system mode\n");
	mode->type |= CUSTOM_VIDEO_TIMING_SYSTEM;
	return false;
}

//============================================================
//  custom_video::set_timing
//============================================================

bool custom_video::set_timing(modeline *mode)
{
	char modeline_txt[256]={'\x00'};
	
	switch (custom_method)
	{
		case CUSTOM_VIDEO_TIMING_ATI_LEGACY:
			if (ati_set_modeline(mode))
			{
				log_verbose("ATI legacy timing %s\n", modeline_print(mode, modeline_txt, MS_FULL));
				return true;
			}
			break;
		
		case CUSTOM_VIDEO_TIMING_ATI_ADL:
			if (adl_set_modeline(m_device_name, mode, mode->interlace != m_backup_mode.interlace? MODELINE_UPDATE_LIST : MODELINE_UPDATE))
			{
				log_verbose("ATI ADL timing %s\n", modeline_print(mode, modeline_txt, MS_FULL));
				return true;
			}
			break;

		case CUSTOM_VIDEO_TIMING_POWERSTRIP:
			// In case -ps_timing is provided, pass it as raw string
			if (m_user_mode.type & CUSTOM_VIDEO_TIMING_POWERSTRIP)
				ps_set_monitor_timing_string(ps_monitor_index(m_device_name), (char*)ps_timing);
			// Otherwise pass it as modeline
			else
				ps_set_modeline(ps_monitor_index(m_device_name), mode);
			
			log_verbose("Powerstrip timing %s\n", modeline_print(mode, modeline_txt, MS_FULL));
			Sleep(100);
			return true;
			break;

		default:
			break;
	}
	return false;
}

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
			ati_refresh_timings();
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

bool custom_video::update_timing(modeline *mode)
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

//============================================================
//  custom_video::parse_timing
//============================================================

bool custom_video_parse_timing(char *timing_string, modeline *user_mode)
{
	char modeline_txt[256]={'\x00'};

	if (!strcmp(timing_string, "auto"))
		return false;

	if (strstr(timing_string, "="))
	{
		// Powerstrip timing string
		MonitorTiming timing;
		ps_read_timing_string(timing_string, &timing);
		ps_pstiming_to_modeline(&timing, user_mode);
		user_mode->type |= CUSTOM_VIDEO_TIMING_POWERSTRIP;
		log_verbose("SwitchRes: ps_string: %s (%s)\n", timing_string, modeline_print(user_mode, modeline_txt, MS_PARAMS));
	}
	else
	{
		// Normal modeline
		modeline_parse(timing_string, user_mode);
		log_verbose("SwitchRes: modeline: %s \n", modeline_print(user_mode, modeline_txt, MS_PARAMS));
	}

	return true;
}

//============================================================
//  custom_video::parse_pci_id
//============================================================

int custom_video::parse_pci_id(char *device_id, int *vendor, int *device)
{
	return sscanf(device_id, "PCI\\VEN_%x&DEV_%x", vendor, device);
}

//============================================================
//  custom_get_backup_mode
//============================================================

modeline *custom_video::get_backup_mode()
{
	return &m_backup_mode;
}
