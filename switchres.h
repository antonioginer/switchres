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
	bool   modeline_generation;
	bool   monitor_orientation;
	bool   effective_orientation;
	bool   desktop_rotated;
	bool   monitor_rotates_cw;
	float  monitor_aspect;
	int    monitor_count;
	int    pclock_min;
	int    pclock_align;
	int    interlace;
	int    doublescan;
	int    width;
	int    height;
	int    refresh;
	int    super_width;
	bool   lock_unsupported_modes;
	bool   lock_system_modes;
	bool   refresh_dont_care;
	float  sync_refresh_tolerance;
} config_settings;

#include "monitor.h"
#include "modeline.h"

typedef struct switchres_manager
{
	struct config_settings cs;
	struct game_info game;
	struct modeline best_mode;
	struct modeline user_mode;
	struct monitor_range range[MAX_RANGES];
	struct modeline video_modes[MAX_MODELINES];
} switchres;

#endif
