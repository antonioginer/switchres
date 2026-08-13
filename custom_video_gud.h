/**************************************************************

     custom_video_gud.h - GUD indirect display interface

     ---------------------------------------------------------

     Switchres   Modeline generation engine for emulation

     License     GPL-2.0+
     Copyright   2010-2021 Chris Kennedy, Antonio Giner,
                           Alexandre Wodarczyk, Gil Delescluse

 **************************************************************/

#ifndef __CUSTOM_VIDEO_GUD__
#define __CUSTOM_VIDEO_GUD__

#include <string>
#include <vector>
#include "custom_video.h"

//============================================================
//  gud_timing
//============================================================
//
// Backend for a Generic USB Display driven by the gud-windows IddCx driver.
//
// Unlike every other Windows backend here, this one does not push timing into
// a graphics driver through a vendor interface. The display is reached over
// USB and its timings live in a file the driver watches:
//
//     C:\ProgramData\gud-windows\modelines.ini    written here, read by driver
//     C:\ProgramData\gud-windows\modes.active     written by driver, read here
//
// Adding a mode means writing a modeline and waiting for the driver to
// re-enumerate the monitor. Setting one is an ordinary ChangeDisplaySettingsEx
// through display_windows, because by then Windows knows the mode.
//
// get_timing() matters more here than on other backends. Windows reports every
// mode this display has as progressive -- an IddCx target mode set as
// interlaced comes back progressive, so a 480i timing looks like 480p and its
// line rate reads as 31 kHz rather than 15.75. Left uncorrected, Switchres
// rules a 15 kHz monitor's own modes out of range. modes.active carries what
// the timings really are.

class gud_timing : public custom_video
{
	public:
		gud_timing(char *device_name, char *device_id, custom_video_settings *vs);
		~gud_timing() {};

		const char *api_name() { return "GUD"; }
		bool init();
		int caps() { return CUSTOM_VIDEO_CAPS_ADD | CUSTOM_VIDEO_CAPS_UPDATE; }

		bool add_mode(modeline *mode);
		bool delete_mode(modeline *mode);
		bool update_mode(modeline *mode);
		bool process_modelist(std::vector<modeline *> modelist);

		bool get_timing(modeline *mode);
		bool set_timing(modeline *mode);

		// True if the driver's published mode list names this device. Used by
		// the factory before constructing one, so that no other display --
		// including another USB-attached one -- is claimed by this backend.
		static bool is_gud_display(const char *device_id);

	private:
		bool read_active(std::vector<modeline> &list);
		bool write_ini();
		bool wait_for_mode(int width, int height, bool present);

		char m_device_name[32] = {};
		char m_device_id[128] = {};

		// The modes this backend owns. Everything else in modelines.ini is
		// somebody's hand-written entry and is preserved untouched.
		std::vector<modeline> m_managed;
};

#endif
