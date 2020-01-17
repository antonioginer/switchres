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

#define CUSTOM_VIDEO_TIMING_SYSTEM      0x00000010


const auto log_verbose = printf;
const auto log_error = printf;
const auto log_info = printf;

bool effective_orientation() { return false; }


//============================================================
//  switchres_manager::switchres_manager
//============================================================

switchres_manager::switchres_manager()
{
	// Set Switchres default config options
	sprintf(cs.monitor, "generic_15");
	sprintf(cs.orientation, "horizontal");
	sprintf(cs.modeline, "auto");
	sprintf(cs.crt_range0, "auto");
	sprintf(cs.crt_range1, "auto");
	sprintf(cs.crt_range2, "auto");
	sprintf(cs.crt_range3, "auto");
	sprintf(cs.crt_range4, "auto");
	sprintf(cs.crt_range5, "auto");
	sprintf(cs.crt_range6, "auto");
	sprintf(cs.crt_range7, "auto");
	sprintf(cs.crt_range8, "auto");
	sprintf(cs.crt_range9, "auto");
	sprintf(cs.lcd_range, "auto");
	cs.monitor_rotates_cw = false;
	cs.monitor_count = 1;

	sprintf(ds.screen, "auto");
	ds.lock_unsupported_modes = true;
	ds.lock_system_modes = true;
	ds.refresh_dont_care = false;

	// Set modeline generator default options
	gs.modeline_generation = true;
	gs.width = 0;
	gs.height = 0;
	gs.refresh = 0;
	gs.interlace = true;
	gs.doublescan = true;
	gs.pclock_min = 0.0;
	gs.rotation = false;
	gs.monitor_aspect = STANDARD_CRT_ASPECT;
	gs.refresh_tolerance = 2.0f;
	gs.super_width = 2560;
}

//============================================================
//  switchres_manager::init
//============================================================

void switchres_manager::init()
{
	log_verbose("SwitchRes: v%s, Monitor: %s, Orientation: %s, Modeline generation: %s\n",
		SWITCHRES_VERSION, cs.monitor, cs.orientation, gs.modeline_generation?"enabled":"disabled");

	// Get user defined modeline
	if (gs.modeline_generation)
	{
		modeline_parse(cs.modeline, &user_mode);
		user_mode.type |= MODE_USER_DEF;
	}

	// Get monitor specs
	if (user_mode.hactive)
	{
		modeline_to_monitor_range(range, &user_mode);
		monitor_show_range(range);
	}
	else
		get_monitor_specs();

	m_display_factory = new display_manager();
	m_display = m_display_factory->make();
}


//============================================================
//  switchres_manager::get_monitor_specs
//============================================================

int switchres_manager::get_monitor_specs()
{
	char default_monitor[] = "generic_15";
	
	memset(&range[0], 0, sizeof(struct monitor_range) * MAX_RANGES);

	if (!strcmp(cs.monitor, "custom"))
	{
		monitor_fill_range(&range[0],cs.crt_range0);
		monitor_fill_range(&range[1],cs.crt_range1);
		monitor_fill_range(&range[2],cs.crt_range2);
		monitor_fill_range(&range[3],cs.crt_range3);
		monitor_fill_range(&range[4],cs.crt_range4);
		monitor_fill_range(&range[5],cs.crt_range5);
		monitor_fill_range(&range[6],cs.crt_range6);
		monitor_fill_range(&range[7],cs.crt_range7);
		monitor_fill_range(&range[8],cs.crt_range8);
		monitor_fill_range(&range[9],cs.crt_range9);
	}
	else if (!strcmp(cs.monitor, "lcd"))
		monitor_fill_lcd_range(&range[0],cs.lcd_range);

	else if (monitor_set_preset(cs.monitor, range) == 0)
		monitor_set_preset(default_monitor, range);

	return 0;
}


//============================================================
//  switchres_manager::get_video_mode
//============================================================

bool switchres_manager::get_video_mode()
{
	modeline s_mode;
	modeline t_mode;
	char modeline[256]={'\x00'};
	char result[256]={'\x00'};
	int i = 0, j = 0, table_size = 0;

	gs.rotation = effective_orientation();

	log_verbose("SwitchRes: v%s:[%s] Calculating best video mode for %dx%d@%.6f orientation: %s\n",
						SWITCHRES_VERSION, game.name, game.width, game.height, game.refresh,
						gs.rotation?"rotated":"normal");

	best_mode = {};
	best_mode.result.weight |= R_OUT_OF_RANGE;
	s_mode.hactive = game.vector?1:normalize(game.width, 8);
	s_mode.vactive = game.vector?1:game.height;
	s_mode.vfreq = game.refresh;

	for (auto mode : m_display->video_modes)
	{
		// apply options to mode type
		if (!gs.modeline_generation)
			mode.type &= ~XYV_EDITABLE;

		if (ds.refresh_dont_care)
			mode.type |= V_FREQ_EDITABLE;
		
		if (ds.lock_system_modes && (mode.type & CUSTOM_VIDEO_TIMING_SYSTEM) && !(mode.type & MODE_DESKTOP) && !(mode.type & MODE_USER_DEF))
			mode.type |= MODE_DISABLED;

		log_verbose("\nSwitchRes: %s%4d%sx%s%4d%s_%s%d=%.6fHz%s%s\n",
			mode.type & X_RES_EDITABLE?"(":"[", mode.width, mode.type & X_RES_EDITABLE?")":"]",
			mode.type & Y_RES_EDITABLE?"(":"[", mode.height, mode.type & Y_RES_EDITABLE?")":"]",
			mode.type & V_FREQ_EDITABLE?"(":"[", mode.refresh, mode.vfreq, mode.type & V_FREQ_EDITABLE?")":"]",
			mode.type & MODE_DISABLED?" - locked":"");

		// now get the mode if allowed
		if (!(mode.type & MODE_DISABLED))
		{
			for (j = 0 ; j < MAX_RANGES ; j++)
			{
				if (range[j].hfreq_min)
				{
					t_mode = mode;
					modeline_create(&s_mode, &t_mode, &range[j], &gs);
					t_mode.range = j;

					log_verbose("%s\n", modeline_result(&t_mode, result));

					if (modeline_compare(&t_mode, &best_mode))
						best_mode = t_mode;
				}
			}
		}
	}

	if (best_mode.result.weight & R_OUT_OF_RANGE)
	{
		log_error("SwitchRes: could not find a video mode that meets your specs\n");
		return false;
	}

	log_info("\nSwitchRes: [%s] (%d) %s (%dx%d@%.6f)->(%dx%d@%.6f)\n", game.name, game.screens, game.orientation?"vertical":"horizontal",
		game.width, game.height, game.refresh, best_mode.hactive, best_mode.vactive, best_mode.vfreq);

	log_verbose("%s\n", modeline_result(&best_mode, result));
	if (gs.modeline_generation)
		log_verbose("SwitchRes: Modeline %s\n", modeline_print(&best_mode, modeline, MS_FULL));

	return true;
}

