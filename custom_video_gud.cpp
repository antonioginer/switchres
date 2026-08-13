/**************************************************************

     custom_video_gud.cpp - GUD indirect display interface

     ---------------------------------------------------------

     Switchres   Modeline generation engine for emulation

     License     GPL-2.0+
     Copyright   2010-2021 Chris Kennedy, Antonio Giner,
                           Alexandre Wodarczyk, Gil Delescluse

 **************************************************************/

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <algorithm>
#include "custom_video_gud.h"
#include "log.h"

#define GUD_INI    "C:\\ProgramData\\gud-windows\\modelines.ini"
#define GUD_ACTIVE "C:\\ProgramData\\gud-windows\\modes.active"

// Entries this backend owns carry this prefix. Anything else in the INI is
// somebody's hand-written modeline and is copied through untouched -- the
// driver treats the file as authoritative, so clobbering it would silently
// throw away work.
#define GUD_KEY_PREFIX "sr_"

// How long to wait for the driver to notice the INI and re-enumerate the
// monitor. It polls on a 2 s timer and arrival is deferred a further 2 s, so
// the floor is about 4 s; 15 gives room without hanging a game launch forever.
#define GUD_WAIT_MS 15000

//============================================================
//  gud_timing::gud_timing
//============================================================

gud_timing::gud_timing(char *device_name, char *device_id, custom_video_settings *vs)
{
	snprintf(m_device_name, sizeof(m_device_name), "%s", device_name);
	snprintf(m_device_id, sizeof(m_device_id), "%s", device_id ? device_id : "");
	m_vs = *vs;
}

//============================================================
//  usb_vid_pid
//============================================================
//
// Pull "VID_xxxx&PID_xxxx" out of a Windows device id or hardware id. Both
// spellings of the same device carry it, and the revision suffix does not
// always agree between them, so this is the part worth comparing.

static bool usb_vid_pid(const char *s, char *out, size_t len)
{
	if (s == nullptr)
		return false;

	// Device ids are conventionally upper case but nothing guarantees it.
	char up[256];
	size_t n = 0;
	for (; s[n] != '\0' && n < sizeof(up) - 1; n++)
		up[n] = (char)toupper((unsigned char)s[n]);
	up[n] = '\0';

	const char *v = strstr(up, "VID_");
	if (v == nullptr)
		return false;

	const char *p = strstr(v, "PID_");
	if (p == nullptr || p - v > 16)
		return false;

	// VID_xxxx&PID_xxxx is 17 characters.
	if (strlen(p) < 8 || len < 18)
		return false;

	snprintf(out, len, "%.4s&%.8s", v + 4, p);
	return true;
}

//============================================================
//  gud_timing::is_gud_display
//============================================================
//
// The display is one of ours if the gud-windows driver has published a mode
// list and that list names this device.
//
// Checking the name matters. More than one USB-attached display can exist on a
// machine -- a DisplayLink adapter, a second indirect display -- and claiming
// one of those would mean writing generated modelines into a driver that has
// nothing to do with it. Matching on a bare "USB\" prefix is not enough, and
// matching on a fixed VID/PID would tie this to one board when the driver
// serves any GUD device.

bool gud_timing::is_gud_display(const char *device_id)
{
	char want[24];
	if (!usb_vid_pid(device_id, want, sizeof(want)))
		return false;

	FILE *f = fopen(GUD_ACTIVE, "r");
	if (f == nullptr)
		return false;

	char line[512];
	bool match = false;

	while (fgets(line, sizeof(line), f))
	{
		// "; device = USB\VID_xxxx&PID_xxxx&REV_xxxx"
		const char *p = strstr(line, "device");
		if (p == nullptr || line[0] != ';')
		{
			// Only the header is worth reading; stop at the modelines.
			if (line[0] == '[')
				break;
			continue;
		}

		char have[24];
		if (usb_vid_pid(p, have, sizeof(have)) && strcmp(have, want) == 0)
			match = true;
		break;
	}

	fclose(f);
	return match;
}

//============================================================
//  gud_timing::init
//============================================================

bool gud_timing::init()
{
	std::vector<modeline> list;
	if (!read_active(list))
	{
		log_error("GUD: could not read %s\n", GUD_ACTIVE);
		return false;
	}

	log_verbose("GUD: %s: driver publishes %d mode(s) in %s\n",
		m_device_name, (int)list.size(), GUD_ACTIVE);
	return true;
}

//============================================================
//  gud_timing::read_active
//============================================================
//
// The driver's own view of what it is running, in the same spelling as the
// INI, so switchres's parser reads it directly.

bool gud_timing::read_active(std::vector<modeline> &list)
{
	FILE *f = fopen(GUD_ACTIVE, "r");
	if (f == nullptr)
		return false;

	char line[512];
	while (fgets(line, sizeof(line), f))
	{
		char *p = line;
		while (*p == ' ' || *p == '\t') p++;
		if (*p == ';' || *p == '#' || *p == '[' || *p == '\r' || *p == '\n' || *p == '\0')
			continue;

		modeline m = {};
		if (modeline_parse(p, &m) != 0)
			list.push_back(m);
	}
	fclose(f);
	return true;
}

//============================================================
//  gud_timing::get_timing
//============================================================
//
// The reason this backend earns its place even before it adds anything.
//
// Windows reports every mode this display has as progressive: an IddCx target
// mode set as interlaced comes back progressive, so 648x480i60 arrives here as
// 480p at 60 Hz, whose line rate computes as 31.5 kHz rather than the 15.75 it
// actually runs. Switchres then finds a 15 kHz monitor's own modes out of
// range and quietly picks something else -- a 480-line request lands on a
// 240-line mode with half the lines thrown away.
//
// The driver knows the truth and writes it down. Fill it in here.

bool gud_timing::get_timing(modeline *mode)
{
	std::vector<modeline> list;
	if (!read_active(list))
		return false;

	for (auto &m : list)
	{
		// Windows carries refresh as an integer, so compare against the
		// rounded vertical frequency rather than expecting equality. One Hz of
		// slack covers the rounding and no more -- two modes at one geometry
		// closer together than that are not distinguishable through this API
		// in either direction.
		if (m.hactive != mode->width || m.vactive != mode->height)
			continue;
		if (abs((int)lround(m.vfreq) - mode->refresh) > 1)
			continue;

		int width = mode->width, height = mode->height, refresh = mode->refresh;
		unsigned type = mode->type;

		*mode = m;

		// Keep what the OS said about identity; take everything about timing
		// from the driver.
		mode->width = width;
		mode->height = height;
		mode->refresh = refresh;
		mode->type = type | CUSTOM_VIDEO_TIMING_GUD;

		return true;
	}

	return false;
}

//============================================================
//  gud_timing::set_timing
//============================================================
//
// Nothing to do. Other backends program timing into a graphics driver here;
// this display's timing was already delivered by writing the modeline, and
// selecting it is an ordinary ChangeDisplaySettingsEx that display_windows
// makes for us.

bool gud_timing::set_timing(modeline *)
{
	return true;
}

//============================================================
//  gud_timing::write_ini
//============================================================
//
// Rewrite only this backend's entries, preserving every other line.

bool gud_timing::write_ini()
{
	std::vector<std::string> keep;
	bool have_section = false;

	FILE *f = fopen(GUD_INI, "r");
	if (f != nullptr)
	{
		char line[512];
		while (fgets(line, sizeof(line), f))
		{
			std::string s(line);
			while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
				s.pop_back();

			const char *p = s.c_str();
			while (*p == ' ' || *p == '\t') p++;

			if (_strnicmp(p, "[modelines]", 11) == 0)
			{
				have_section = true;
				keep.push_back(s);
				continue;
			}
			if (_strnicmp(p, GUD_KEY_PREFIX, strlen(GUD_KEY_PREFIX)) == 0)
				continue;   // ours, and about to be rewritten

			keep.push_back(s);
		}
		fclose(f);
	}

	f = fopen(GUD_INI, "w");
	if (f == nullptr)
	{
		log_error("GUD: cannot write %s\n", GUD_INI);
		return false;
	}

	for (auto &s : keep)
		fprintf(f, "%s\n", s.c_str());
	if (!have_section)
		fprintf(f, "[modelines]\n");

	int n = 0;
	for (auto &m : m_managed)
	{
		char buf[256];
		modeline_print(&m, buf, MS_LABEL | MS_PARAMS);
		fprintf(f, "%s%d = ModeLine %s\n", GUD_KEY_PREFIX, n++, buf);
	}

	fclose(f);
	return true;
}

//============================================================
//  gud_timing::wait_for_mode
//============================================================
//
// The driver polls the INI on a timer and then takes the monitor away and
// brings it back, because IddCx reads a monitor's mode list once, at arrival.
// So a written modeline is not immediately settable and there is no
// notification to wait on -- poll for it to appear.

bool gud_timing::wait_for_mode(int width, int height, bool present)
{
	DWORD start = GetTickCount();

	while (GetTickCount() - start < GUD_WAIT_MS)
	{
		bool found = false;
		DEVMODEA dm = {};
		dm.dmSize = sizeof(dm);

		for (int i = 0; EnumDisplaySettingsA(m_device_name, i, &dm) != 0; i++)
			if ((int)dm.dmPelsWidth == width && (int)dm.dmPelsHeight == height)
			{
				found = true;
				break;
			}

		if (found == present)
		{
			log_verbose("GUD: mode %dx%d %s after %d ms\n", width, height,
				present ? "appeared" : "removed", (int)(GetTickCount() - start));
			return true;
		}
		Sleep(250);
	}

	log_error("GUD: timed out waiting for %dx%d to %s\n", width, height,
		present ? "appear" : "be removed");
	return false;
}

//============================================================
//  gud_timing::process_modelist
//============================================================
//
// The batch entry point, and the one that matters: display_manager::flush_modes
// calls only this. add_mode() and friends are reached through the wrapper API
// and are implemented in terms of the same steps, but a CLI or emulator run
// arrives here.
//
// Batching is worth doing rather than looping over add_mode. Every write to the
// INI costs a monitor departure and arrival -- the swap chain goes and the CRT
// blanks -- so a list of five modes should cost one of those, not five.

bool gud_timing::process_modelist(std::vector<modeline *> modelist)
{
	bool error = false;
	bool changed = false;
	std::vector<modeline *> added;

	for (auto &mode : modelist)
	{
		if (mode->type & MODE_DELETE)
		{
			auto it = std::find_if(m_managed.begin(), m_managed.end(),
				[&](const modeline &m) {
					return m.hactive == mode->hactive && m.vactive == mode->vactive
					    && m.htotal == mode->htotal && m.vtotal == mode->vtotal;
				});
			if (it != m_managed.end())
			{
				m_managed.erase(it);
				changed = true;
			}
		}
		else if (mode->type & (MODE_ADD | MODE_UPDATE))
		{
			auto it = std::find_if(m_managed.begin(), m_managed.end(),
				[&](const modeline &m) {
					return m.hactive == mode->hactive && m.vactive == mode->vactive;
				});
			if (it != m_managed.end())
				*it = *mode;
			else
				m_managed.push_back(*mode);

			added.push_back(mode);
			changed = true;
		}

		mode->type &= ~MODE_ERROR;
		mode->type |= CUSTOM_VIDEO_TIMING_GUD;
	}

	if (!changed)
		return true;

	if (!write_ini())
	{
		for (auto &mode : modelist)
			mode->type |= MODE_ERROR;
		return false;
	}

	// One wait for the whole batch. Waiting on the last added geometry is
	// enough: the driver rebuilds the store from the file in one pass, so if
	// any of them arrived, all of them did.
	if (!added.empty())
	{
		modeline *last = added.back();
		if (!wait_for_mode(last->hactive, last->vactive, true))
		{
			for (auto &mode : added)
				mode->type |= MODE_ERROR;
			error = true;
		}
	}

	// See add_mode for why the interlace flag is dropped once the mode is the
	// OS's to set.
	for (auto &mode : added)
		if (!(mode->type & MODE_ERROR))
			mode->interlace = 0;

	return !error;
}

//============================================================
//  gud_timing::add_mode
//============================================================

bool gud_timing::add_mode(modeline *mode)
{
	m_managed.push_back(*mode);

	if (!write_ini())
		return false;

	if (!wait_for_mode(mode->hactive, mode->vactive, true))
	{
		m_managed.pop_back();
		write_ini();
		return false;
	}

	// From here on Windows owns this mode, and Windows believes it is
	// progressive -- it has no way not to. display_windows builds its DEVMODE
	// from mode->interlace and sets DM_INTERLACED accordingly, and a
	// ChangeDisplaySettingsEx asking for an interlaced mode against this
	// display returns DISP_CHANGE_BADMODE. Tested, not assumed.
	//
	// Dropping the flag here loses nothing real: interlace on a GUD display is
	// a property of the link between the driver and the board, not of anything
	// the OS does. The driver keeps it -- it is in the modeline just written --
	// and the device interleaves on read.
	mode->interlace = 0;

	return true;
}

//============================================================
//  gud_timing::delete_mode
//============================================================

bool gud_timing::delete_mode(modeline *mode)
{
	auto it = std::find_if(m_managed.begin(), m_managed.end(),
		[&](const modeline &m) {
			return m.hactive == mode->hactive && m.vactive == mode->vactive
			    && m.htotal == mode->htotal && m.vtotal == mode->vtotal;
		});

	if (it == m_managed.end())
		return true;    // never ours; nothing to undo

	int w = it->hactive, h = it->vactive;
	m_managed.erase(it);

	if (!write_ini())
		return false;

	wait_for_mode(w, h, false);
	return true;
}

//============================================================
//  gud_timing::update_mode
//============================================================

bool gud_timing::update_mode(modeline *mode)
{
	for (auto &m : m_managed)
		if (m.hactive == mode->hactive && m.vactive == mode->vactive)
		{
			m = *mode;
			bool ok = write_ini();
			mode->interlace = 0;   // see add_mode
			return ok;
		}

	return add_mode(mode);
}
