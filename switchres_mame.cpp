/**************************************************************

   switchres_mame.cpp - SwichRes MAME hook

   ---------------------------------------------------------

   Switchres   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2020 Chris Kennedy, Antonio Giner,
                         Alexandre Wodarczyk, Gil Delescluse

 **************************************************************/

#include "emu.h"
#include "emuopts.h"
#include "../frontend/mame/mameopts.h"
#include "config.h"
#include "rendutil.h"


//============================================================
//  switchres_get_game_info
//============================================================

void switchres_get_game_info(running_machine &machine)
{
	emu_options &options = machine.options();
	game_info *game = &machine.switchres.game;
	const game_driver *game_drv = &machine.system();
	const screen_device *screen;

	// Get game information
	sprintf(game->name, "%s", options.system_name());
	if (game->name[0] == 0) sprintf(game->name, "empty");

	machine_config config(*game_drv, options);
	screen = screen_device_iterator(config.root_device()).first();

	// Fill in current video mode settings
	game->orientation = effective_orientation(machine);

	if (screen->screen_type() == SCREEN_TYPE_VECTOR)
	{
		game->vector = 1;
		game->width = 640;
		game->height = 480;
	}

	// Output width and height only for games that are not vector
	else
	{
		const rectangle &visarea = screen->visible_area();
		int w = visarea.max_x - visarea.min_x + 1;
		int h = visarea.max_y - visarea.min_y + 1;
		game->width = game->orientation?h:w;
		game->height = game->orientation?w:h;
	}

	game->refresh = ATTOSECONDS_TO_HZ(screen->refresh_attoseconds());

	// Check for multiple screens
	screen_device_iterator iter(config.root_device());
	game->screens = iter.count();
}

//============================================================
//  effective_orientation
//============================================================

bool effective_orientation(running_machine &machine)
{
	config_settings *cs = &machine.switchres.cs;
	const game_driver *game = &machine.system();
	emu_options &options = machine.options();
	render_target *target = machine.render().first_target();
	bool game_orientation = ((game->flags & machine_flags::MASK_ORIENTATION) & ORIENTATION_SWAP_XY);

	if (target)
		cs->monitor_orientation = ((target->orientation() & machine_flags::MASK_ORIENTATION) & ORIENTATION_SWAP_XY? 1:0) ^ cs->desktop_rotated;
	else if (!strcmp(options.orientation(), "horizontal"))
		cs->monitor_orientation = 0;
	else if (!strcmp(options.orientation(), "vertical"))
		cs->monitor_orientation = 1;
	else if (!strcmp(options.orientation(), "rotate") || !strcmp(options.orientation(), "rotate_r"))
	{
		cs->monitor_orientation = game_orientation;
		cs->monitor_rotates_cw = 0;
	}
	else if (!strcmp(options.orientation(), "rotate_l"))
	{
		cs->monitor_orientation = game_orientation;
		cs->monitor_rotates_cw = 1;
	}

	return game_orientation ^ cs->monitor_orientation;
}

//============================================================
//  switchres_check_resolution_change
//============================================================

bool switchres_check_resolution_change(running_machine &machine)
{
	game_info *game = &machine.switchres.game;
	config_settings *cs = &machine.switchres.cs;
	
	int new_width = game->width;
	int new_height = game->height;
	float new_vfreq = game->refresh;
	bool new_orientation = effective_orientation(machine);

	screen_device_iterator scriter(machine.root_device());
	if (scriter.count())
	{
		screen_device *screen = scriter.first();
		if (screen->frame_number())
		{
			const rectangle &visarea = screen->visible_area();
			new_width = new_orientation? visarea.height() : visarea.width();
			new_height = new_orientation? visarea.width() : visarea.height();
			new_vfreq = ATTOSECONDS_TO_HZ(screen->frame_period().m_attoseconds);
		}
	}

	if (game->width != new_width || game->height != new_height || new_vfreq != game->refresh || cs->effective_orientation != new_orientation)
	{
		osd_printf_verbose("SwitchRes: Resolution change from %dx%d@%f %s to %dx%d@%f %s\n",
			game->width, game->height, game->refresh, cs->effective_orientation?"rotated":"normal", new_width, new_height, new_vfreq, new_orientation?"rotated":"normal");

		game->width = new_width;
		game->height = new_height;
		game->refresh = new_vfreq;

		return true;
	}
	return false;
}

//============================================================
//  switchres_set_options
//============================================================

void switchres_set_options(running_machine &machine)
{
	config_settings *cs = &machine.switchres.cs;
	bool native_orientation = ((machine.system().flags & machine_flags::MASK_ORIENTATION) & ORIENTATION_SWAP_XY);
	bool must_rotate = effective_orientation(machine) ^ cs->desktop_rotated;
	modeline *best_mode = &machine.switchres.best_mode;

	// Set rotation options
	set_option(machine, OPTION_ROTATE, true);
	if (cs->monitor_rotates_cw)
	{
		set_option(machine, OPTION_ROL, (!native_orientation & must_rotate));
		set_option(machine, OPTION_AUTOROL, !must_rotate);
		set_option(machine, OPTION_ROR, false);
		set_option(machine, OPTION_AUTOROR, false);
	}
	else
	{
		set_option(machine, OPTION_ROR, (!native_orientation & must_rotate));
		set_option(machine, OPTION_AUTOROR, !must_rotate);
		set_option(machine, OPTION_ROL, false);
		set_option(machine, OPTION_AUTOROL, false);
	}

	// Set scaling/stretching options
	set_option(machine, OPTION_KEEPASPECT, true);
	set_option(machine, OPTION_UNEVENSTRETCH, best_mode->result.weight & R_RES_STRETCH);
	set_option(machine, OPTION_UNEVENSTRETCHX, (!(best_mode->result.weight & R_RES_STRETCH) && (best_mode->width >= machine.options().super_width())));

	// Update target if it's already initialized
	render_target *target = machine.render().first_target();
	if (target)
	{
		if (machine.options().uneven_stretch())
			target->set_scale_mode(SCALE_FRACTIONAL);
		else if(machine.options().uneven_stretch_x())
			target->set_scale_mode(SCALE_FRACTIONAL_X);
		else if(machine.options().uneven_stretch_y())
			target->set_scale_mode(SCALE_FRACTIONAL_Y);
		else
			target->set_scale_mode(SCALE_INTEGER);
	}
}

//============================================================
// set_option - option setting wrapper
//============================================================

void set_option(running_machine &machine, const char *option_ID, bool state)
{
	emu_options &options = machine.options();

	options.set_value(option_ID, state, OPTION_PRIORITY_SWITCHRES);
	osd_printf_verbose("SwitchRes: Setting option -%s%s\n", machine.options().bool_value(option_ID)?"":"no", option_ID);
}
