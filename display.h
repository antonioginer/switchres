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
	char   api[32];
	bool   modeline_generation;
	bool   lock_unsupported_modes;
	bool   lock_system_modes;
	bool   refresh_dont_care;
	char   ps_timing[256];
} display_settings;


class display_manager
{
public:

	display_manager() {};
	virtual ~display_manager()
	{
		restore_modes();
		if (m_video) delete m_video;
		if (m_factory) delete m_factory;
	};

	display_manager *make();
	virtual bool init(display_settings *ds);
	int caps();

	// getters
	modeline user_mode() const { return m_user_mode; }

	// setters
	void set_user_mode(modeline *mode) { m_user_mode = *mode; }

	// options
	display_settings *m_ds = 0;
	bool m_desktop_rotated;

	// custom video backend
	custom_video *m_factory = 0;
	custom_video *m_video = 0;

	// mode setting interface
	bool add_mode(modeline *mode);
	bool delete_mode(modeline *mode);
	bool update_mode(modeline *mode);
	bool set_mode(modeline *mode);
	void log_mode(modeline *mode);

	// mode list handling
	bool filter_modes();
	bool restore_modes();

	// mode list
	std::vector<modeline> video_modes = {};
	std::vector<modeline> backup_modes = {};
	modeline desktop_mode = {};

private:

	display_manager *m_display_manager = 0;
	modeline m_user_mode = {};
};

#endif
