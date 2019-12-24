/**************************************************************

   switchres.h - SwichRes general header

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   GroovyMAME  Integration of SwitchRes into the MAME project
               Some reworked patches from SailorSat's CabMAME

   License     GPL-2.0+
   Copyright   2010-2016 - Chris Kennedy, Antonio Giner

 **************************************************************/

#ifndef __SWITCHRES_H__
#define __SWITCHRES_H__

#include "monitor.h"
#include "modeline.h"

//============================================================
//  CONSTANTS
//============================================================

#define SWITCHRES_VERSION "0.017o"

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
	char   connector[32];
	char   monitor[32];
	bool   monitor_orientation;
	bool   desktop_rotated;
	bool   monitor_rotates_cw;
	int    monitor_count;
	bool   lock_unsupported_modes;
	bool   lock_system_modes;
	bool   refresh_dont_care;
} config_settings;


class switchres_manager
{
public:

	switchres_manager();
	~switchres_manager();

	config_settings cs;
	generator_settings gs;
	game_info game;
	modeline best_mode;
	modeline user_mode;
	monitor_range range[MAX_RANGES];
	modeline video_modes[MAX_MODELINES];

	void init();
	int get_monitor_specs();
	bool get_video_mode();


private:
};

#endif