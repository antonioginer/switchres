/**************************************************************

   switchres.cpp - SwichRes core routines

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2016 - Chris Kennedy, Antonio Giner

 **************************************************************/

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "switchres.h"

#define CUSTOM_VIDEO_TIMING_SYSTEM      0x00000010


const auto log_verbose = printf;
const auto log_error = printf;
const auto log_info = printf;


//============================================================
//  switchres_manager::init
//============================================================

void switchres_manager::init()
{
	emu_options &options = machine.options();

	log_verbose("SwitchRes: v%s, Monitor: %s, Orientation: %s, Modeline generation: %s\n",
		SWITCHRES_VERSION, options.monitor(), options.orientation(), options.modeline_generation()?"enabled":"disabled");

	// Get user defined modeline
	if (options.modeline_generation())
	{
		modeline_parse(options.modeline(), &user_mode);
		user_mode.type |= MODE_USER_DEF;
	}

	// Get monitor specs
	sprintf(cs.monitor, "%s", options.monitor());
	sprintf(cs.connector, "%s", options.connector());
	for (int i = 0; cs.monitor[i]; i++) cs.monitor[i] = tolower(cs.monitor[i]);
	if (user_mode.hactive)
	{
		modeline_to_monitor_range(range, &user_mode);
		monitor_show_range(range);
	}
	else
		get_monitor_specs();

	// Get rest of config options
	gs.modeline_generation = options.modeline_generation();
	gs.doublescan = options.doublescan();
	gs.interlace = options.interlace();
	cs.lock_system_modes = options.lock_system_modes();
	cs.lock_unsupported_modes = options.lock_unsupported_modes();
	cs.refresh_dont_care = options.refresh_dont_care();
	gs.super_width = options.super_width();
	sscanf(options.sync_refresh_tolerance(), "%f", &gs.sync_refresh_tolerance);
	float pclock_min;
	sscanf(options.dotclock_min(), "%f", &pclock_min);
	gs.pclock_min = pclock_min * 1000000;
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
		monitor_fill_range(&range[0],machine.options().crt_range0());
		monitor_fill_range(&range[1],machine.options().crt_range1());
		monitor_fill_range(&range[2],machine.options().crt_range2());
		monitor_fill_range(&range[3],machine.options().crt_range3());
		monitor_fill_range(&range[4],machine.options().crt_range4());
		monitor_fill_range(&range[5],machine.options().crt_range5());
		monitor_fill_range(&range[6],machine.options().crt_range6());
		monitor_fill_range(&range[7],machine.options().crt_range7());
		monitor_fill_range(&range[8],machine.options().crt_range8());
		monitor_fill_range(&range[9],machine.options().crt_range9());
	}
	else if (!strcmp(cs.monitor, "lcd"))
		monitor_fill_lcd_range(&range[0],machine.options().lcd_range());

	else if (monitor_set_preset(cs.monitor, range) == 0)
		monitor_set_preset(default_monitor, range);

	return 0;
}


//============================================================
//  switchres_manager::get_video_mode
//============================================================

bool switchres_manager::get_video_mode()
{
	modeline *mode;
	modeline source_mode, *s_mode = &source_mode;
	modeline target_mode, *t_mode = &target_mode;
	char modeline[256]={'\x00'};
	char result[256]={'\x00'};
	int i = 0, j = 0, table_size = 0;

	gs.effective_orientation = effective_orientation(machine);

	log_verbose("SwitchRes: v%s:[%s] Calculating best video mode for %dx%d@%.6f orientation: %s\n",
						SWITCHRES_VERSION, game.name, game.width, game.height, game.refresh,
						gs.effective_orientation?"rotated":"normal");

	memset(&best_mode, 0, sizeof(struct modeline));
	best_mode.result.weight |= R_OUT_OF_RANGE;
	s_mode->hactive = game.vector?1:normalize(game.width, 8);
	s_mode->vactive = game.vector?1:game.height;
	s_mode->vfreq = game.refresh;

	if (user_mode.hactive)
	{
		table_size = 1;
		mode = &user_mode;
	}
	else
	{
		i = 1;
		table_size = MAX_MODELINES;
		mode = &video_modes[i];
	}

	while (mode->width && i < table_size)
	{
		// apply options to mode type
		if (!gs.modeline_generation)
			mode->type &= ~XYV_EDITABLE;

		if (cs.refresh_dont_care)
			mode->type |= V_FREQ_EDITABLE;
		
		if (cs.lock_system_modes && (mode->type & CUSTOM_VIDEO_TIMING_SYSTEM) && !(mode->type & MODE_DESKTOP) && !(mode->type & MODE_USER_DEF))
			mode->type |= MODE_DISABLED;

		log_verbose("\nSwitchRes: %s%4d%sx%s%4d%s_%s%d=%.6fHz%s%s\n",
			mode->type & X_RES_EDITABLE?"(":"[", mode->width, mode->type & X_RES_EDITABLE?")":"]",
			mode->type & Y_RES_EDITABLE?"(":"[", mode->height, mode->type & Y_RES_EDITABLE?")":"]",
			mode->type & V_FREQ_EDITABLE?"(":"[", mode->refresh, mode->vfreq, mode->type & V_FREQ_EDITABLE?")":"]",
			mode->type & MODE_DISABLED?" - locked":"");

		// now get the mode if allowed
		if (!(mode->type & MODE_DISABLED))
		{
			for (j = 0 ; j < MAX_RANGES ; j++)
			{
				if (range[j].hfreq_min)
				{
					memcpy(t_mode, mode, sizeof(struct modeline));
					modeline_create(s_mode, t_mode, &range[j], &gs);
					t_mode->range = j;

					log_verbose("%s\n", modeline_result(t_mode, result));

					if (modeline_compare(t_mode, &best_mode))
						memcpy(&best_mode, t_mode, sizeof(struct modeline));
				}
			}
		}
		mode++;
		i++;
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

