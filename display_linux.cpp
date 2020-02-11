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
//  linux_display::linux_display
//============================================================

linux_display::linux_display()
{
}

//============================================================
//  linux_display::~linux_display
//============================================================

linux_display::~linux_display()
{
}

//============================================================
//  linux_display::init
//============================================================

bool linux_display::init(display_settings *ds)
{
	//init the display manager
	m_ds = ds;

	set_factory(new custom_video);
	set_custom_video(factory()->make(ds->screen, NULL, 0, NULL));
	if (video()) video()->init();

        // Build our display's mode list
	video_modes.clear();
	backup_modes.clear();

	// It is not needed to call get_desktop_mode, it is already performed by the get_available_video_modes function
	//get_desktop_mode();
	get_available_video_modes();

	filter_modes();

	return true;
}

//============================================================
//  linux_display::get_desktop_mode
//============================================================

bool linux_display::get_desktop_mode()
{
	if (video() == NULL) 
		return false;

        return true;
}


//============================================================
//  linux_display::set_desktop_mode
//============================================================

bool linux_display::set_desktop_mode(modeline *mode, int flags)
{
	if (!mode) 
		return false;

	if (video() == NULL) 
		return false;

        return video()->set_timing(mode);
}

//============================================================
//  linux_display::restore_desktop_mode
//============================================================

bool linux_display::restore_desktop_mode()
{
	if (video() == NULL) 
		return false;

	modeline mode;
	memset(&mode, 0, sizeof(struct modeline));
	mode.type &= MODE_DESKTOP;

        return video()->set_timing(&mode);
}

//============================================================
//  linux_display::get_available_video_modes
//============================================================

int linux_display::get_available_video_modes()
{
	if (video() == NULL) 
		return false;

	for (;;) {
		modeline mode;
		memset(&mode, 0, sizeof(struct modeline));

		video()->get_timing(&mode);
		if ( mode.type == 0 )
			break;
		
		if (mode.type & MODE_DESKTOP)
			memcpy(&desktop_mode, &mode, sizeof(modeline));

		video_modes.push_back(mode);
		backup_modes.push_back(mode);
	};

	return true;
}
