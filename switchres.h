/**************************************************************

   switchres.h - SwichRes general header

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2016 - Chris Kennedy, Antonio Giner

 **************************************************************/

#ifndef __SWITCHRES_H__
#define __SWITCHRES_H__

#include "monitor.h"
#include "modeline.h"
#include "display.h"

//============================================================
//  CONSTANTS
//============================================================

#define SWITCHRES_VERSION "1.00"

//============================================================
//  TYPE DEFINITIONS
//============================================================

typedef struct config_settings
{
	char   monitor[32];
	char   orientation[32];
	char   modeline[256];
	char   crt_range[MAX_RANGES][256];
	char   lcd_range[256];
	bool   monitor_rotates_cw;
} config_settings;


class switchres_manager
{
public:

	switchres_manager();
	~switchres_manager()
	{
		if (m_display_factory) delete m_display_factory;
	};

	// getters
	modeline *best_mode() const { return m_best_mode; }
	display_manager *display() const { return m_display; }

	// setters (switchres manager)
	void set_monitor(const char *preset) { sprintf(cs.monitor, preset); }
	void set_orientation(const char *orientation) { sprintf(cs.orientation, orientation); }
	void set_modeline(const char *modeline) { sprintf(cs.modeline, modeline); }
	void set_crt_range(int i, const char *range) { sprintf(cs.crt_range[i], range); }
	void set_lcd_range(const char *range) { sprintf(cs.lcd_range, range); }
	void set_monitor_rotates_cw(bool value) { cs.monitor_rotates_cw = value; }

	// setters (display manager)
	void set_screen(const char *screen) { sprintf(ds.screen, screen); }
	void set_api(const char *api) { sprintf(ds.api, api); }
	void set_modeline_generation(bool value) { ds.modeline_generation = value; }
	void set_lock_unsupported_modes(bool value) { ds.lock_unsupported_modes = value; }
	void set_lock_system_modes(bool value) { ds.lock_system_modes = value; }
	void set_refresh_dont_care(bool value) { ds.refresh_dont_care = value; }
	void set_ps_timing(const char *ps_timing) { sprintf(ds.ps_timing, ps_timing); }

	//setters (modeline generator)
	void set_interlace(bool value) { gs.interlace = value; }
	void set_doublescan(bool value) { gs.doublescan = value; }
	void set_dotclock_min(double value) { gs.pclock_min = value * 1000000; }
	void set_refresh_tolerance(double value) { gs.refresh_tolerance = value; }
	void set_super_width(int value) { gs.super_width = value; }
	void set_rotation(bool value) { gs.rotation = value; }
	void set_monitor_aspect(double value) { gs.monitor_aspect = value; }

	// interface
	void init();
	modeline *get_video_mode(int width, int height, float refresh, bool rotated);

	//settings
	config_settings cs;
	display_settings ds;
	generator_settings gs;

private:
	monitor_range range[MAX_RANGES];

	display_manager *m_display_factory = 0;
	display_manager *m_display = 0;
	modeline *m_best_mode = 0;

	int get_monitor_specs();
};


#endif