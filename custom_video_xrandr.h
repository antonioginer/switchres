/**************************************************************

   custom_video_xrandr.h - Linux XRANDR video management layer

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2020 - Chris Kennedy, Antonio Giner, Alexandre Wodarczyk

 **************************************************************/

// X11 Xrandr headers
#include <X11/extensions/Xrandr.h>

#include <string.h>

#include "display.h"

class xrandr_timing : public custom_video
{
	public:
		xrandr_timing(char *device_name, char *param);
		~xrandr_timing();
		const char *api_name() { return "XRANDR"; }
		int caps() { return CUSTOM_VIDEO_CAPS_ADD; }
		bool init();

                bool add_mode(modeline *mode);
                bool delete_mode(modeline *mode);

		bool get_timing(modeline *mode);
		bool set_timing(modeline *mode);

		static int xerrors;
	private:
		bool restore_mode();
		bool set_mode(modeline *mode);

		int video_modes_position = 0;

		char m_device_name[32];
		char m_param[128];

		Display *dpy;
		Window root;

		short original_rate;
		Rotation original_rotation;
		SizeID original_size_id;
		int width = 0;
		int height = 0;

		int gmoutput_primary = 0;
		int gmoutput_total = 0;
		int gmoutput_mode = 0;

		int (*old_error_handler)(Display *, XErrorEvent *);
};
