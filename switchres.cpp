/**************************************************************

   switchres.cpp - SwichRes core routines

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2016 - Chris Kennedy, Antonio Giner

 **************************************************************/

#include <stdio.h>
#include <string.h>
#include "switchres.h"
#include "log.h"

//============================================================
//  switchres_manager::switchres_manager
//============================================================

switchres_manager::switchres_manager()
{
	// Set Switchres default config options
	set_monitor("generic_15");
	set_orientation("horizontal");
	set_modeline("auto");
	set_lcd_range("auto");
	for (int i = 0; i++ < MAX_RANGES;) set_crt_range(i, "auto");
	set_monitor_rotates_cw(false);

	// Set display manager default options
	set_screen("auto");
	set_modeline_generation(true);
	set_lock_unsupported_modes(true);
	set_lock_system_modes(true);
	set_refresh_dont_care(false);

	// Set modeline generator default options
	set_interlace(true);
	set_doublescan(true);
	set_dotclock_min(0.0f);
	set_rotation(false);
	set_monitor_aspect(STANDARD_CRT_ASPECT);
	set_refresh_tolerance(2.0f);
	set_super_width(2560);
}

//============================================================
//  switchres_manager::init
//============================================================

void switchres_manager::init()
{
	log_verbose("Switchres: v%s, Monitor: %s, Orientation: %s, Modeline generation: %s\n",
		SWITCHRES_VERSION, cs.monitor, cs.orientation, ds.modeline_generation?"enabled":"disabled");

	// Create our display manager
	m_display_factory = new display_manager();
	m_display = m_display_factory->make();

	// Get user defined modeline
	modeline user_mode = {};
	if (ds.modeline_generation)
	{
		if (modeline_parse(cs.modeline, &user_mode))
		{
			user_mode.type |= MODE_USER_DEF;
			m_display->set_user_mode(&user_mode);
		}
	}

	// Get monitor specs
	if (user_mode.hactive)
	{
		modeline_to_monitor_range(range, &user_mode);
		monitor_show_range(range);
	}
	else
		get_monitor_specs();
}


//============================================================
//  switchres_manager::get_monitor_specs
//============================================================

int switchres_manager::get_monitor_specs()
{
	char default_monitor[] = "generic_15";
	
	memset(&range[0], 0, sizeof(struct monitor_range) * MAX_RANGES);

	if (!strcmp(cs.monitor, "custom"))
		for (int i = 0; i++ < MAX_RANGES;) monitor_fill_range(&range[i], cs.crt_range[i]);

	else if (!strcmp(cs.monitor, "lcd"))
		monitor_fill_lcd_range(&range[0],cs.lcd_range);

	else if (monitor_set_preset(cs.monitor, range) == 0)
		monitor_set_preset(default_monitor, range);

	return 0;
}


//============================================================
//  switchres_manager::get_video_mode
//============================================================

modeline *switchres_manager::get_video_mode(int width, int height, float refresh, bool rotated)
{
	modeline s_mode = {};
	modeline t_mode = {};
	modeline best_mode = {};
	char result[256]={'\x00'};

	log_verbose("Switchres: v%s: Calculating best video mode for %dx%d@%.6f orientation: %s\n",
						SWITCHRES_VERSION, width, height, refresh, rotated?"rotated":"normal");

	best_mode.result.weight |= R_OUT_OF_RANGE;
//	s_mode.hactive = game.vector?1:normalize(game.width, 8);
//	s_mode.vactive = game.vector?1:game.height;
//	s_mode.vfreq = game.refresh;
	s_mode.hactive = normalize(width, 8);
	s_mode.vactive = height;
	s_mode.vfreq = refresh;
	gs.rotation = rotated;

	// Create a dummy mode entry if allowed
	if (m_display->caps() & CUSTOM_VIDEO_CAPS_ADD && ds.modeline_generation)
	{
		modeline new_mode = {};
		new_mode.type = XYV_EDITABLE | V_FREQ_EDITABLE | SCAN_EDITABLE | MODE_NEW;
		m_display->video_modes.push_back(new_mode);
	}

	// Run through our mode list and find the most suitable mode
	for (auto &mode : m_display->video_modes)
	{
		log_verbose("\nSwitchres: %s%4d%sx%s%4d%s_%s%d=%.6fHz%s%s\n",
			mode.type & X_RES_EDITABLE?"(":"[", mode.width, mode.type & X_RES_EDITABLE?")":"]",
			mode.type & Y_RES_EDITABLE?"(":"[", mode.height, mode.type & Y_RES_EDITABLE?")":"]",
			mode.type & V_FREQ_EDITABLE?"(":"[", mode.refresh, mode.vfreq, mode.type & V_FREQ_EDITABLE?")":"]",
			mode.type & MODE_DISABLED?" - locked":"");

		// now get the mode if allowed
		if (!(mode.type & MODE_DISABLED))
		{
			for (int i = 0 ; i < MAX_RANGES ; i++)
			{
				if (range[i].hfreq_min)
				{
					t_mode = mode;
					modeline user_mode = m_display->user_mode();

					// init all editable fields with source or user values
					if (t_mode.type & X_RES_EDITABLE)
						t_mode.hactive = user_mode.width? user_mode.width : s_mode.hactive;

					if (t_mode.type & Y_RES_EDITABLE)
						t_mode.vactive = user_mode.height? user_mode.height : s_mode.vactive;

					if (mode.type & V_FREQ_EDITABLE)
						t_mode.vfreq = s_mode.vfreq;

					// lock resolution fields if required
					if (user_mode.width) t_mode.type &= ~X_RES_EDITABLE;
					if (user_mode.height) t_mode.type &= ~Y_RES_EDITABLE;

					modeline_create(&s_mode, &t_mode, &range[i], &gs);
					t_mode.range = i;

					log_verbose("%s\n", modeline_result(&t_mode, result));

					if (modeline_compare(&t_mode, &best_mode))
					{
						best_mode = t_mode;
						m_best_mode = &mode;
					}
				}
			}
		}
	}

	// If we didn't need to create a new mode, remove our dummy entry
	if (m_display->caps() & CUSTOM_VIDEO_CAPS_ADD && ds.modeline_generation && !(best_mode.type & MODE_NEW))
		m_display->video_modes.pop_back();

	// If we didn't find a suitable mode, exit now
	if (best_mode.result.weight & R_OUT_OF_RANGE)
	{
		m_best_mode = 0;
		log_error("Switchres: could not find a video mode that meets your specs\n");
		return nullptr;
	}

	log_info("\nSwitchres: %s (%dx%d@%.6f)->(%dx%d@%.6f)\n", rotated?"vertical":"horizontal",
		width, height, refresh, best_mode.hactive, best_mode.vactive, best_mode.vfreq);

	log_verbose("%s\n", modeline_result(&best_mode, result));

	// Copy the new modeline to our mode list
	if (ds.modeline_generation && (best_mode.type & V_FREQ_EDITABLE))
	{
		if (best_mode.type & MODE_NEW)
		{
			best_mode.width = best_mode.hactive;
			best_mode.height = best_mode.vactive;
			best_mode.refresh = int(best_mode.vfreq);
			// lock new mode
			best_mode.type &= ~(X_RES_EDITABLE | Y_RES_EDITABLE | (m_display->caps() & CUSTOM_VIDEO_CAPS_UPDATE? 0 : V_FREQ_EDITABLE));
		}
		else
			best_mode.type |= MODE_UPDATED;

		char modeline[256]={'\x00'};
		log_verbose("Switchres: Modeline %s\n", modeline_print(&best_mode, modeline, MS_FULL));
	}

	*m_best_mode = best_mode;
	return m_best_mode;
}
