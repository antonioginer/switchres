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

	display_manager display;

	void init();
	int get_monitor_specs();
	bool get_video_mode();


private:
};


#endif