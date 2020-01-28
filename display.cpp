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

	return true;
}

//============================================================
//  display_manager::caps
//============================================================

int display_manager::caps()
{
	if (video)
		return video->caps();
	else
		return CUSTOM_VIDEO_CAPS_ADD;
}

//============================================================
//  display_manager::add_mode
//============================================================

bool display_manager::add_mode(modeline *mode)
{
	// Add new mode
	log_verbose("Switchres: adding ");

	if (!video->add_mode(mode))
	{
		log_verbose(": error adding video mode\n");
		return false;		
	}

	log_mode(mode);
	return true;
}

//============================================================
//  display_manager::update_mode
//============================================================

bool display_manager::update_mode(modeline *mode)
{
	// Apply new timings
	log_verbose("Switchres: updating ");

	if (!video->update_mode(mode))
	{
		log_verbose(": error updating video timings\n");
		return false;
	}

	log_mode(mode);
	return true;
}

//============================================================
//  display_manager::log_mode
//============================================================

void display_manager::log_mode(modeline *mode)
{
	char modeline_txt[256];
	log_verbose("%s timing %s\n", video->api_name(), modeline_print(mode, modeline_txt, MS_FULL));
}
