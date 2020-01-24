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

#include <stdio.h>

#include "display.h"

class linux_display : public display_manager
{
	public:
		linux_display() {};
		~linux_display() {};
		bool init(display_settings *ds);

	private:
		bool init_osd();
		bool modeline_setup();
		bool modeline_remove();
		bool modeline_reset();
		bool resolution_change();
		bool add_custom_video_mode(modeline *mode);
		bool set_custom_video_mode(modeline *mode);
		int del_custom_video_mode(modeline *mode);

		int mode_count = 1;

		bool get_desktop_mode();
		bool set_desktop_mode(modeline *mode, int flags);
		bool restore_desktop_mode();
		int get_available_video_modes();

		char m_device_name[32];
		char m_device_id[128];
		char m_device_key[128];
		//DEVMODEA m_devmode;

		#define XRANDR_ARGS ""
		#define min(a,b)({ __typeof__ (a) _a = (a);__typeof__ (b) _b = (b);_a < _b ? _a : _b; })

		#define XRANDR_TIMING      0x00000020
		//extern int fd;

		//============================================================
		//  XRANDR
		//============================================================

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

		int xerrors = 0;

		static int error_handler (Display *dpy, XErrorEvent *err)
		{
		        //xerrors++; // TODO, xerror handling inside functions
		        return 0;
		} /* xorg_error_handler() */
};
