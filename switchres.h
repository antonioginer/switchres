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
	int    monitor_count;
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

	// setters
	bool set_monitor_preset(char *preset);

	void init();
	modeline *get_video_mode(int width, int height, float refresh, bool rotated);


	config_settings cs;
	display_settings ds;
	generator_settings gs;
	monitor_range range[MAX_RANGES];

private:
	display_manager *m_display_factory = 0;
	display_manager *m_display = 0;
	modeline *m_best_mode = 0;

	int get_monitor_specs();
};


#endif