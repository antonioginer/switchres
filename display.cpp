/**************************************************************

   display.cpp - Display manager

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2016 - Chris Kennedy, Antonio Giner

 **************************************************************/

#include <stdio.h>
#include "display.h"
#if defined(_WIN32)
#include "display_windows.h"
#elif defined(__linux__)
#include "display_linux.h"
#endif
#include "log.h"

//============================================================
//  display_manager::make
//============================================================

display_manager *display_manager::make()
{

#if defined(_WIN32)
	m_display_manager = new windows_display();
#elif defined(__linux__)
	m_display_manager = new linux_display();
#endif

	if (m_display_manager)
	{
		return m_display_manager;
	}

	return nullptr;
}

//============================================================
//  display_manager::init
//============================================================

bool display_manager::init(display_settings *ds)
{
	// Initialize display settings
	sprintf(ds->screen, "ram");

	m_ds = ds;

	return true;
}

//============================================================
//  display_manager::caps
//============================================================

int display_manager::caps()
{
	if (video())
		return video()->caps();
	else
		return CUSTOM_VIDEO_CAPS_ADD;
}

//============================================================
//  display_manager::add_mode
//============================================================

bool display_manager::add_mode(modeline *mode)
{
	if (video() == nullptr)
		return false;

	// Add new mode
	if (!video()->add_mode(mode))
	{
		log_verbose("Switchres: error adding mode ");
		log_mode(mode);
		return false;		
	}

	log_verbose("Switchres: added ");
	log_mode(mode);

	return true;
}

//============================================================
//  display_manager::delete_mode
//============================================================

bool display_manager::delete_mode(modeline *mode)
{
	if (video() == nullptr)
		return false;

	if (!video()->delete_mode(mode))
	{
		log_verbose("Switchres: error deleting mode ");
		log_mode(mode);
		return false;
	}

	log_verbose("Switchres: deleted ");
	log_mode(mode);
	return true;
}

//============================================================
//  display_manager::update_mode
//============================================================

bool display_manager::update_mode(modeline *mode)
{
	if (video() == nullptr)
		return false;

	// Apply new timings
	if (!video()->update_mode(mode))
	{
		log_verbose("Switchres: error updating mode ");
		log_mode(mode);
		return false;
	}

	log_verbose("Switchres: updated ");
	log_mode(mode);
	return true;
}

//============================================================
//  display_manager::set_mode
//============================================================

bool display_manager::set_mode(modeline *)
{
	return false;
}

//============================================================
//  display_manager::log_mode
//============================================================

void display_manager::log_mode(modeline *mode)
{
	char modeline_txt[256];
	log_verbose("%s timing %s\n", video()->api_name(), modeline_print(mode, modeline_txt, MS_FULL));
}

//============================================================
//  display_manager::restore_modes
//============================================================

bool display_manager::restore_modes()
{
	bool error = false;

	// First, delete all modes we've added
	while (video_modes.size() > backup_modes.size())
	{
		delete_mode(&video_modes.back());
		video_modes.pop_back();
	}

	// Now restore all modes which timings have been modified
	for (unsigned i = video_modes.size(); i-- > 0; )
	{
		// Reset work fields
		video_modes[i].type = backup_modes[i].type = 0;
		video_modes[i].range = backup_modes[i].range = 0;

		if (memcmp(&video_modes[i], &backup_modes[i], sizeof(modeline) - sizeof(mode_result)) != 0)
		{
			video_modes[i] = backup_modes[i];
			if (!video()->update_mode(&video_modes[i]))
			{
				log_verbose("Switchres: error restoring mode ");
				log_mode(&video_modes[i]);
				error = true;
			}
			else
			{
				log_verbose("Switchres: restored ");
				log_mode(&video_modes[i]);
			}
		}
	}

	return !error;
}

//============================================================
//  display_manager::filter_modes
//============================================================

bool display_manager::filter_modes()
{
	for (auto &mode : video_modes)
	{
		// apply options to mode type
		if (m_ds->refresh_dont_care)
			mode.type |= V_FREQ_EDITABLE;

		if ((caps() & CUSTOM_VIDEO_CAPS_UPDATE))
			mode.type |= V_FREQ_EDITABLE;

		if (caps() & CUSTOM_VIDEO_CAPS_SCAN_EDITABLE)
			mode.type |= SCAN_EDITABLE;

		if (!m_ds->modeline_generation)
			mode.type &= ~(XYV_EDITABLE | SCAN_EDITABLE);

		if ((mode.type & MODE_DESKTOP) && !(caps() & CUSTOM_VIDEO_CAPS_DESKTOP_EDITABLE))
			mode.type &= ~V_FREQ_EDITABLE;

		if (m_ds->lock_system_modes && (mode.type & CUSTOM_VIDEO_TIMING_SYSTEM))
			mode.type |= MODE_DISABLED;

		// Lock all modes that don't match the user's -resolution rules
		if (m_user_mode.width != 0 || m_user_mode.height != 0 || m_user_mode.refresh == !0)
		{
			if (!( (mode.width == m_user_mode.width || (mode.type & X_RES_EDITABLE) || m_user_mode.width == 0)
				&& (mode.height == m_user_mode.height || (mode.type & Y_RES_EDITABLE) || m_user_mode.height == 0)
				&& (mode.refresh == m_user_mode.refresh || (mode.type & V_FREQ_EDITABLE) || m_user_mode.refresh == 0) ))
				mode.type |= MODE_DISABLED;
			else
				mode.type &= ~MODE_DISABLED;
		}

		// Make sure to unlock the desktop mode as fallback
		if (mode.type & MODE_DESKTOP)
			mode.type &= ~MODE_DISABLED;
	}

	return true;
}