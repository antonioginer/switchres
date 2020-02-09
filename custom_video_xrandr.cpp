/**************************************************************

   video_xrandr_xrandr.cpp - Linux XRANDR video management layer

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2020 - Chris Kennedy, Antonio Giner, Alexandre Wodarczyk

 **************************************************************/

#include <stdio.h>
#include "custom_video_xrandr.h"
#include "log.h"

//============================================================
//  xrandr_timing::xrandr_timing
//============================================================

xrandr_timing::xrandr_timing(char *device_name, char *param)
{
	log_verbose("AWK : xrandr_timint init\n");

}

//============================================================
//  xrandr_timing::init
//============================================================

bool xrandr_timing::init()
{
	//config_settings *cs = &machine.switchres.cs;
	//game_info *game = &machine.switchres.game;
	//modeline *mode_table = machine.switchres.video_modes;
	//modeline *user_mode = &machine.switchres.user_mode;
	//monitor_range *range = machine.switchres.range;
	const char * aspect;
	char resolution[32]={'\x00'};

	//sdl_options &options = downcast<sdl_options &>(machine.options());

	// Initialize structures and config settings
	//memset(cs, 0, sizeof(struct config_settings));
	//memset(game, 0, sizeof(struct game_info));

	// Init Switchres common info
	//init(machine);

	// Complete config settings
	//strcpy(resolution, options.resolution());
	//cs->monitor_count = options.numscreens();

	// Get current resolution
	int screen = -1;

	// dpy is global to reduce open/close calls, resource is freed when modeline is reset
	dpy = XOpenDisplay(NULL);
	int major_version, minor_version;
	XRRQueryVersion(dpy, &major_version, &minor_version);
	log_verbose("SwitchRes: xrandr version %d.%d\n",major_version,minor_version);

	// select current display and root window
	// root is global to reduce open/close calls, resource is freed when modeline is reset
	screen = DefaultScreen(dpy); // multiple screen ScreenCount (dpy)
	root = RootWindow(dpy, screen);
	XRRScreenResources *res = XRRGetScreenResourcesCurrent(dpy, root);

	// get screen size, rate and rotation from screen configuration
	XRRScreenConfiguration *sc = XRRGetScreenInfo(dpy, root);
	original_rate = XRRConfigCurrentRate(sc);
	original_size_id = XRRConfigCurrentConfiguration(sc, &original_rotation);
	XRRFreeScreenConfigInfo(sc);

	Rotation current_rotation = 0;
	for (int o = 0; o < res->noutput && !gmoutput_mode; o++)
	{
		XRROutputInfo *output_info = XRRGetOutputInfo (dpy, res, res->outputs[o]);
		if (!output_info)
			log_error("SwitchRes: error could not get output 0x%x information\n", (uint) res->outputs[o]);

		// first connected output
		if (output_info->connection == RR_Connected)
		{
			for (int j = 0; j < output_info->nmode && !gmoutput_mode; j++)
			{
				if ( output_info->crtc )
				{
					XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(dpy, res, output_info->crtc);
					current_rotation = crtc_info->rotation;
					//TODO if (!strcmp(cs->connector, "auto") || !strcmp(cs->connector,output_info->name))
					{
						// connector name is kept but not necessary due to global gmoutput_primary varial, optimization can happen here
						log_verbose("SwitchRes: Found output connector '%s'\n", output_info->name);
						gmoutput_primary = o;
						gmoutput_total++;
					}
					for (int m = 0; m < res->nmode && !gmoutput_mode; m++)
					{
						XRRModeInfo *mode = &res->modes[m];
						// get screen mode
						if (crtc_info->mode == mode->id)
						{
							gmoutput_mode = mode->id;
							width = crtc_info->x + crtc_info->width;
							height = crtc_info->y + crtc_info->height;
						}
					}
				}
				if (current_rotation & 0xe) // screen rotation is left or right
				{
					log_verbose("Switchres: desktop rotation is %s\n",(current_rotation & 0x2)?"left":((current_rotation & 0x8)?"right":"inverted"));
					// TODO cs->desktop_rotated = 1;
				}
			}
		}
		XRRFreeOutputInfo(output_info);
	}
	XRRFreeScreenResources(res);

	//handle no screen detected case
	if (gmoutput_total == 0)
	{
		log_error("Switchres: error, no screen detected\n");
		return false;
	}

	// Get per window resolution
	//TODO strcpy(resolution, strcmp(options.resolution(0), "auto")? options.resolution(0) : options.resolution());

	// Get monitor aspect
	//TODO aspect = strcmp(options.aspect(0), "auto")? options.aspect(0) : options.aspect();
	if (strcmp(aspect, "auto"))
	{
		float num, den;
		sscanf(aspect, "%f:%f", &num, &den);
		//TODO cs->monitor_aspect = cs->desktop_rotated? den/num : num/den;
	}
	/* TODO
	else
		cs->monitor_aspect = STANDARD_CRT_ASPECT;
		*/

	// Create dummy mode table
	/* TODO
	mode_table[1].width = mode_table[1].height = 1;
	mode_table[1].refresh = 60;
	mode_table[1].vfreq = mode_table[1].refresh;
	mode_table[1].hactive = mode_table[1].vactive = 1;
	mode_table[1].type = XYV_EDITABLE | XRANDR_TIMING | (cs->desktop_rotated? MODE_ROTATED : MODE_OK);

	if (user_mode->hactive)
	{
		user_mode->width = user_mode->hactive;
		user_mode->height = user_mode->vactive;
		user_mode->refresh = int(user_mode->refresh);
		user_mode->type = XRANDR_TIMING | MODE_USER_DEF | (cs->desktop_rotated? MODE_ROTATED : MODE_OK);
	}

	// Create automatic specs and force resolution for LCD monitors
	if (!strcmp(cs->monitor, "lcd"))
	{
		modeline current;
		memset(&current, 0, sizeof(struct modeline));

		log_verbose("SwitchRes: Creating automatic specs for LCD based on VESA GTF\n");
		current.width = width;
		current.height = height;
		current.refresh = 60;
		modeline_vesa_gtf(&current);
		modeline_to_monitor_range(range, &current);
		monitor_show_range(range);

		sprintf(resolution, "%dx%d@%d", current.width, current.height, current.refresh);
	}
	// Otherwise (non-LCD), convert the user defined modeline into a -resolution option
	else if (user_mode->hactive)
		sprintf(resolution, "%dx%d", user_mode->hactive, user_mode->vactive);

	// Get resolution from ini
	if (strcmp(resolution, "auto"))
	{
		log_verbose("SwitchRes: -resolution was set at command line or in .ini file as %s\n", resolution);

		if ((sscanf(resolution, "%dx%d@%d", &cs->width, &cs->height, &cs->refresh) < 3) &&
			((!strstr(resolution, "x") || (sscanf(resolution, "%dx%d", &cs->width, &cs->height) != 2))))
				osd_printf_info("SwitchRes: illegal -resolution value: %s\n", resolution);
		else
		{
			// Add the user's resolution to our table
			if (!user_mode->hactive)
			{
				mode_table[1].width = mode_table[1].hactive = cs->width? cs->width : 1;
				mode_table[1].height = mode_table[1].vactive = cs->height? cs->height : 1;
				mode_table[1].refresh = cs->refresh? int(cs->refresh) : 60;
				mode_table[1].vfreq = mode_table[1].refresh;
				mode_table[1].type |= MODE_USER_DEF;
				if (cs->width) mode_table[1].type &= ~X_RES_EDITABLE;
				if (cs->height) mode_table[1].type &= ~Y_RES_EDITABLE;
			}
		}
	}
	// Get game info
	//get_game_info(machine);
	*/
	return true;
}

//============================================================
//  xrandr_timing::modeline_setup
//============================================================

bool xrandr_timing::modeline_setup()
{
	//modeline *best_mode = &machine.switchres.best_mode;
	//modeline *mode_table = machine.switchres.video_modes;
	//sdl_options &options = downcast<sdl_options &>(machine.options());
	//sdl_osd_interface &osd = downcast<sdl_osd_interface &>(machine.osd());
	//std::string error_string;

	log_verbose("\nSwitchRes: Entering modeline_setup\n");

	// Find most suitable video mode and generate a modeline for it if we're allowed
	//if (!get_video_mode(machine))
	//{
		//set_option_osd(machine, OSDOPTION_SWITCHRES, false);
	//	return false;
	//}

	// Make the new modeline available to the system
	//if (machine.options().modeline_generation())
	{
		// Lock mode before adding it to mode table
		//TODO best_mode->type |= MODE_DISABLED;

		// Check if the same mode had been created already
		int i;
		bool found = false;
		for (i = 2; i <= mode_count; i++)
			//TODO if (!memcmp(&mode_table[i], best_mode, sizeof(modeline) - sizeof(mode_result)))
				found = true;

		// Create the new mode and store it in our table
		if (!found)
		{
			mode_count++;
			//TODO memcpy(&mode_table[mode_count], best_mode, sizeof(modeline));
	//TODO		add_video_xrandr_mode(best_mode);
		}

		// Switch to the new mode
		//TODO set_video_xrandr_mode(best_mode);
	}

	// Set MAME common options
	//set_options(machine);

	// Black frame insertion / multithreading
	//TODO bool black_frame_insertion = options.black_frame_insertion() && best_mode->result.v_scale > 1 && best_mode->vfreq > 100;
	//set_option_osd(machine, OPTION_BLACK_FRAME_INSERTION, black_frame_insertion);

	// Set MAME OSD specific options

	// Vertical synchronization management (autosync)
	// Disable -syncrefresh if our vfreq is scaled or out of syncrefresh_tolerance
	//TODO bool sync_refresh_effective = black_frame_insertion || !((best_mode->result.weight & R_V_FREQ_OFF) || best_mode->result.v_scale > 1);
	//set_option_osd(machine, OPTION_SYNCREFRESH, options.autosync()? sync_refresh_effective : options.sync_refresh());
	//set_option_osd(machine, OSDOPTION_WAITVSYNC, options.sync_refresh()? options.sync_refresh() : options.wait_vsync());

	// Set filter options
	//set_option_osd(machine, OSDOPTION_FILTER, ((best_mode->result.weight & R_RES_STRETCH || best_mode->interlace)));

	// Refresh video options
	//osd.extract_video_config();

	return true;
}

//============================================================
//  xrandr_timing::modeline_remove
//============================================================

bool xrandr_timing::modeline_remove()
{
	return true;
}

//============================================================
//  xrandr_timing::modeline_reset
//============================================================

bool xrandr_timing::modeline_reset()
{
	//modeline *mode_table = machine.switchres.video_modes;

	// Restore desktop resolution
	XRRScreenResources *res = XRRGetScreenResourcesCurrent(dpy, root);
	XRRScreenConfiguration *sc = XRRGetScreenInfo(dpy, root);

	XRRSetScreenConfigAndRate(dpy, sc, root, original_size_id, original_rotation, original_rate, CurrentTime);
	XRRFreeScreenConfigInfo(sc);
	XRRFreeScreenResources(res);

	log_verbose("SwitchRes: xrandr original video mode restored.\n");

	// Remove modelines
	while (mode_count > 1)
	{
		//TODO del_video_xrandr_mode(&mode_table[mode_count]);
		mode_count--;
	}

	XCloseDisplay(dpy);
	return true;
}

//============================================================
//  xrandr_timing::resolution_change
//============================================================

bool xrandr_timing::resolution_change()
{
	//running_machine &machine = window->machine();
	modeline *best_mode = 0; // TODO &machine.switchres.best_mode;
	modeline previous_mode;

	// If there's no pending change, just exit
	//if (!check_resolution_change(machine))
	//	return false;

	// Get the new resolution
	previous_mode = *best_mode;
	//modeline_setup(machine);

	// Only change resolution if the new one is actually different
	if (memcmp(&previous_mode, best_mode, offsetof(modeline, result)))
		return true;

	return false;
}

//============================================================
//  xrandr_timing::add_video_xrandr_mode
//============================================================

bool xrandr_timing::add_custom_video_mode(modeline *mode)
{
	if (!mode)
		return false;

	// Add modeline to interface
	char name[48];
	sprintf(name,"GM-%dx%d_%.6f",mode->hactive, mode->vactive, mode->vfreq); // add ID

	// Setup the xrandr mode structure
	XRRModeInfo xmode;
	xmode.name       = name;
	xmode.nameLength = strlen(name);
	xmode.dotClock   = float(mode->pclock);
	xmode.width      = mode->hactive;
	xmode.hSyncStart = mode->hbegin;
	xmode.hSyncEnd   = mode->hend;
	xmode.hTotal     = mode->htotal;
	xmode.height     = mode->vactive;
	xmode.vSyncStart = mode->vbegin;
	xmode.vSyncEnd   = mode->vend;
	xmode.vTotal     = mode->vtotal;
	xmode.modeFlags  = (mode->interlace?RR_Interlace:0) | (mode->doublescan?RR_DoubleScan:0) | (mode->hsync?RR_HSyncPositive:RR_HSyncNegative) | (mode->vsync?RR_VSyncPositive:RR_VSyncNegative);

	// Create the modeline
	XSync(dpy, False);
	xerrors = 0;
	old_error_handler = XSetErrorHandler(error_handler);
	RRMode gmid = XRRCreateMode(dpy, root, &xmode);
	XSync(dpy, False);
	XSetErrorHandler(old_error_handler);
	if (xerrors)
		log_error("Switchres: xrandr error in %s\n","XRRCreateMode");

	// Add new modeline to primary output
	XRRScreenResources *res = XRRGetScreenResourcesCurrent(dpy, root);

	XSync(dpy, False);
	xerrors = 0;
	old_error_handler = XSetErrorHandler(error_handler);
	XRRAddOutputMode(dpy, res->outputs[gmoutput_primary], gmid);
	XSync(dpy, False);
	XSetErrorHandler(old_error_handler);
	if (xerrors)
		log_error("Switchres: xrandr error in %s\n","XRRAddOutputMode");

	XRRFreeScreenResources(res);
	return true;
}

//============================================================
//  xrandr_timing::set_video_xrandr_mode
//============================================================

bool xrandr_timing::set_custom_video_mode(modeline *mode)
{
	// Use xrandr to switch to new mode. SDL_SetVideoMode doesn't work when (new_width, new_height)==(old_width, old_height)
	char name[48];
	sprintf(name,"GM-%dx%d_%.6f",mode->hactive, mode->vactive, mode->vfreq); // add ID

	XRRScreenResources *res = XRRGetScreenResourcesCurrent(dpy, root);
	XRROutputInfo *output_info = XRRGetOutputInfo(dpy, res, res->outputs[gmoutput_primary]);
	XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(dpy, res, output_info->crtc);

	// Select corresponding mode from modeline, can be enhanced by saving mode index to modeline structure
	XRRModeInfo *xmode=0;
	for (int m = 0; m < res->nmode; m++)
	{
		XRRModeInfo *tmp_mode = &res->modes[m];
		if (!strcmp(name, tmp_mode->name))
		{
			xmode = &res->modes[m];
		}
	}

	// Grab X server to prevent unwanted interaction from the window manager
	XGrabServer(dpy);

	// Disable all CRTCs
	for (int i = 0; i < output_info->ncrtc; i++)
	{
		if (XRRSetCrtcConfig(dpy, res, output_info->crtcs[i], CurrentTime, 0, 0, None, RR_Rotate_0, NULL, 0) != RRSetConfigSuccess)
			log_error("Switchres: xrandr error when disabling CRTC.\n");
	}
	log_verbose("Switchres: CRTC %d: mode %#lx, %ux%u+%d+%d.\n", 0, crtc_info->mode,crtc_info->width, crtc_info->height, crtc_info->x, crtc_info->y);

	// Check if framebuffer size is correct
	int change_resolution = 0;
	if (width < crtc_info->x + mode->hactive)
	{
		width = crtc_info->x + mode->hactive;
		change_resolution = 1;
	}
	if (height < crtc_info->y + mode->vactive)
	{
		height = crtc_info->y + mode->vactive;
		change_resolution = 1;
	}

	// Enlarge the screen size for the new mode
	if (change_resolution)
	{
		log_verbose("Switchres: xrandr change screen size.\n");
		XSync(dpy, False);
		xerrors = 0;
		old_error_handler = XSetErrorHandler(error_handler);
		XRRSetScreenSize(dpy, root, width, height, (25.4 * width) / 96.0, (25.4 * height) / 96.0);
		XSync(dpy, False);
		XSetErrorHandler(old_error_handler);
		if (xerrors)
			log_error("Switchres: xrandr error in %s\n","XRRSetScreenSize");
	}

	// Switch to new modeline
	XSync(dpy, False);
	xerrors = 0;
	old_error_handler = XSetErrorHandler(error_handler);
	XRRSetCrtcConfig(dpy, res, output_info->crtc, CurrentTime , crtc_info->x, crtc_info->y, xmode->id, original_rotation, crtc_info->outputs, crtc_info->noutput);
	XSync(dpy, False);
	XSetErrorHandler(old_error_handler);

	XRRFreeCrtcInfo(crtc_info);

	if (xerrors)
		log_error("Switchres: xrandr error in %s\n","XRRSetCrtcConfig");

	// Release X server, events can be processed now
	XUngrabServer(dpy);

	crtc_info = XRRGetCrtcInfo(dpy, res, output_info->crtc); // recall crtc to settle parameters

	// If the crtc config modeline change fails, revert to original mode (prevents ending with black screen due to all crtc disabled)
	if (crtc_info->mode == 0)
	{
		log_error("Switchres: xrandr resolution switch error, original mode restored\n");
		XRRScreenConfiguration *sc = XRRGetScreenInfo(dpy, root);
		XRRSetScreenConfigAndRate(dpy, sc, root, original_size_id, original_rotation, original_rate, CurrentTime);
		XRRFreeScreenConfigInfo(sc);
	}

	// check, verify current active mode
	for (int m = 0; m < res->nmode; m++)
	{
		XRRModeInfo *mode = &res->modes[m];
		if (mode->id == crtc_info->mode)
		log_verbose("Switchres: xrandr mode (%s) (0x%x) %6.6fMHz\n", mode->name, (int)mode->id,(double)mode->dotClock / 1000000.0);
	}

	XRRFreeCrtcInfo(crtc_info);
	XRRFreeOutputInfo(output_info);
	XRRFreeScreenResources(res);

	return true;
}

//============================================================
//  xrandr_timing::del_video_xrandr_mode
//============================================================

int xrandr_timing::del_custom_video_mode(modeline *mode)
{
	if (!mode)
		return false;

	char name[48];
	sprintf(name,"GM-%dx%d_%.6f",mode->hactive, mode->vactive, mode->vfreq); // add ID

	XRRScreenResources *res = XRRGetScreenResourcesCurrent (dpy, root);

	// Delete modeline
	for (int m = 0; m < res->nmode; m++)
	{
		XRRModeInfo *xmode = &res->modes[m];
		if (!strcmp(name, xmode->name))
		{
			XSync(dpy, False);
			xerrors = 0;
			old_error_handler = XSetErrorHandler(error_handler);
			XRRDeleteOutputMode (dpy, res->outputs[gmoutput_primary], xmode->id);
			if (xerrors)
				log_error("Switchres: xrandr error in %s\n","XRRDeleteOutputMode");

			xerrors = 0;
			XRRDestroyMode (dpy, xmode->id);
			XSync(dpy, False);
			XSetErrorHandler(old_error_handler);
			if (xerrors)
				log_error("Switchres: xrandr error in %s\n","XRRDestroyMode");
		}
	}

	XRRFreeScreenResources(res);

	return true;
}

//============================================================
//  xrandr_timing::add_mode
//============================================================

bool xrandr_timing::add_mode(modeline *mode)
{
        return false;
}

//============================================================
//  xrandr_timing::delete_mode
//============================================================

bool xrandr_timing::delete_mode(modeline *mode)
{
        return false;
}

//============================================================
//  xrandr_timing::get_timing
//============================================================

bool xrandr_timing::get_timing(modeline *mode)
{
        return false;
}

//============================================================
//  xrandr_timing::set_timing
//============================================================

bool xrandr_timing::set_timing(modeline *mode)
{
        return false;
}

//============================================================
//  xrandr_timing::update_mode
//============================================================

bool xrandr_timing::update_mode(modeline *mode)
{
        return false;
}
