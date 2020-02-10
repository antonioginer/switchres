/**************************************************************

   display_linux.cpp - Display manager for Linux

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2020 - Chris Kennedy, Antonio Giner, Alexandre Wodarczyk

 **************************************************************/

#include <stdio.h>
#include "display_linux.h"
#include "log.h"

//============================================================
//  linux_display::init
//============================================================

bool linux_display::init(display_settings *ds)
{
	log_verbose("AWK: display_linux init\n");

	set_factory(new custom_video);
	set_custom_video(factory()->make(ds->screen, NULL, NULL, NULL));
	if (video()) video()->init();

	return true;
}

//============================================================
//  linux_display::get_desktop_mode
//============================================================

bool linux_display::get_desktop_mode()
{
	log_verbose("AWK: display_linux get_desktop_mode\n");
	return true;
}

//============================================================
//  linux_display::set_desktop_mode
//============================================================

bool linux_display::set_desktop_mode(modeline *mode, int flags)
{
	log_verbose("AWK: display_linux set_desktop_mode\n");
	//if (video()) video()->set_custom_video_mode(mode);
	return true;
}

//============================================================
//  linux_display::restore_desktop_mode
//============================================================

bool linux_display::restore_desktop_mode()
{
	log_verbose("AWK: display_linux restore_desktop_mode\n");
	//if (video()) video()->modeline_reset();
	return true;
}

//============================================================
//  linux_display::get_available_video_modes
//============================================================

int linux_display::get_available_video_modes()
{
	log_verbose("AWK: display_linux get_available_video_mode\n");
	return false;
}

