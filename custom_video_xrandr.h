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
                bool update_mode(modeline *mode);

		bool get_timing(modeline *mode);
		bool set_timing(modeline *mode);

		static int m_xerrors;
		static int m_xerrors_flag;

	private:
		bool restore_mode();
		bool detect_connector(int screen_pos);
		bool set_mode(modeline *mode);
		XRRModeInfo *find_mode(modeline *mode);

		int m_video_modes_position = 0;
		char m_device_name[32];

		Display *m_dpy;
		Window m_root;

		short m_original_rate;
		Rotation m_original_rotation;
		SizeID m_original_size_id;
		//int m_width = 0;
		//int m_height = 0;

		int m_desktop_output = -1;
		//long unsigned int m_output_mode = 0;
		RRMode m_desktop_modeid = 0;
		int m_crtc_flags = 0;

		int (*old_error_handler)(Display *, XErrorEvent *);
};
