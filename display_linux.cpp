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
	set_custom_video(factory()->make(m_device_name, NULL, NULL, NULL));
	//if (video()) video()->init();
	return true;
}

//============================================================
//  linux_display::get_desktop_mode
//============================================================

bool linux_display::get_desktop_mode()
{
	return true;
}

//============================================================
//  linux_display::set_desktop_mode
//============================================================

bool linux_display::set_desktop_mode(modeline *mode, int flags)
{
	return true;
}

//============================================================
//  linux_display::restore_desktop_mode
//============================================================

bool linux_display::restore_desktop_mode()
{
	return true;
}

//============================================================
//  linux_display::get_available_video_modes
//============================================================

int linux_display::get_available_video_modes()
{
	return 0;
}

