/**************************************************************

   display.h - Display manager

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2016 - Chris Kennedy, Antonio Giner

 **************************************************************/

#ifndef __DISPLAY_H__
#define __DISPLAY_H__

#include <vector>
#include "modeline.h"
#include "custom_video.h"

typedef struct display_settings
{
	char   screen[32];
	bool   lock_unsupported_modes;
	bool   lock_system_modes;
	bool   refresh_dont_care;
} display_settings;


class display_manager
{
public:

	display_manager() {};
	virtual ~display_manager()
	{
		if (factory) delete factory;
		if (video) delete video;
	};

	display_manager *make();
	virtual bool init(display_settings *ds);
	int caps();

	bool add_mode(modeline *mode);
	bool del_mode(modeline *mode);
	bool update_mode(modeline *mode);
	bool set_mode(modeline *mode);

	std::vector<modeline> video_modes = {};
	modeline desktop_mode = {};

	custom_video *factory = 0;
	custom_video *video = 0;
	
	bool m_lock_unsupported_modes;
	bool m_desktop_rotated;

private:
	display_manager *m_display_manager = 0;

};

#endif
