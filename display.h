/**************************************************************

   display.h - Display manager

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2016 - Chris Kennedy, Antonio Giner

 **************************************************************/

#ifndef __DISPLAY_H__
#define __DISPLAY_H__

#include "modeline.h"
#include "custom_video.h"

class display_manager
{
public:

	display_manager() {};
	~display_manager()
	{
		if (factory) delete factory;
		if (video) delete video;
	};

	modeline video_modes[MAX_MODELINES] = {};
	modeline desktop_mode;

	custom_video *factory = 0;
	custom_video *video = 0;

	display_manager *make();
	virtual bool init(const char *screen_option) { return false; }
	virtual bool get_desktop_mode() { return false; }
	virtual bool set_desktop_mode(modeline *mode, int flags) { return false; }
	virtual bool restore_desktop_mode() { return false; }
	virtual int get_available_video_modes() { return 0; }

	bool m_lock_unsupported_modes;
	bool m_desktop_rotated;

private:
	display_manager *m_display_manager = 0;

};

#endif