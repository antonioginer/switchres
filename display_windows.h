/**************************************************************

   display_windows.h - Display manager for Windows

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2016 - Chris Kennedy, Antonio Giner

 **************************************************************/

#include <windows.h>
#include "display.h"

//============================================================
//  PARAMETERS
//============================================================

// display modes
#define DM_INTERLACED 0x00000002
#define DISPLAY_MAX 16


class windows_display : public display_manager
{
	public:
		windows_display() {};
		bool init(display_settings *ds);

	private:
		bool get_desktop_mode();
		bool set_desktop_mode(modeline *mode, int flags);
		bool restore_desktop_mode();
		int get_available_video_modes();

		char m_device_name[32];
		char m_device_id[128];
		char m_device_key[128];
		DEVMODEA m_devmode;
};
