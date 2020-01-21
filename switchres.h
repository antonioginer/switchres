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

typedef struct game_info
{
	char   name[32];
	int    width;
	int    height;
	float  refresh;
	bool   orientation;
	bool   vector;
	bool   changeres;
	int    screens;
} game_info;


typedef struct config_settings
{
	char   monitor[32];
	char   orientation[32];
	char   modeline[256];
	char   crt_range0[256];
	char   crt_range1[256];
	char   crt_range2[256];
	char   crt_range3[256];
	char   crt_range4[256];
	char   crt_range5[256];
	char   crt_range6[256];
	char   crt_range7[256];
	char   crt_range8[256];
	char   crt_range9[256];
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
		if (m_display) delete m_display;
	};

	config_settings cs;
	display_settings ds;
	generator_settings gs;
	game_info game = {};
	modeline user_mode = {};
	monitor_range range[MAX_RANGES];

	void init();
	modeline *get_video_mode();
	modeline *best_mode() { return m_best_mode; }
	display_manager *display() { return m_display; }

private:
	display_manager *m_display_factory = 0;
	display_manager *m_display = 0;
	modeline *m_best_mode = 0;

	int get_monitor_specs();
};


#endif