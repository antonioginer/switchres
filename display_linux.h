/**************************************************************

   display_linux.h - Display manager for Linux

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2020 - Chris Kennedy, Antonio Giner, Alexandre Wodarczyk

 **************************************************************/

// X11 Xrandr headers
#include <X11/extensions/Xrandr.h>

#include <string.h>

#include "display.h"

class linux_display : public display_manager
{
	public:
		linux_display() {};
		~linux_display() {};
		bool init(display_settings *ds);

	private:

                bool get_desktop_mode();
                bool set_desktop_mode(modeline *mode, int flags);
                bool restore_desktop_mode();
                int get_available_video_modes();

                char m_device_name[32];
                char m_device_id[128];
                char m_device_key[128];
};
