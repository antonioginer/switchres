/**************************************************************

   display.h - Display manager

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2016 - Chris Kennedy, Antonio Giner

 **************************************************************/

#ifndef __DISPLAY_H__
#define __DISPLAY_H__

#include "custom_video.h"

class display_manager
{
public:

	display_manager() {};
	~display_manager()
	{
		if (video) delete video;
	};

	modeline video_modes[MAX_MODELINES];
	modeline desktop_mode;

	custom_video *factory = 0;
	custom_video *video = 0;

	int init(const char *screen_option);
	int get_desktop_mode();
	int set_desktop_mode(modeline *mode, int flags);
	int restore_desktop_mode();
	int get_available_video_modes();

private:
	bool m_lock_unsupported_modes;
	bool m_desktop_rotated;
};

#endif