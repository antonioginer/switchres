/**************************************************************

   modeline.h - Modeline generation header

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   GroovyMAME  Integration of SwitchRes into the MAME project
               Some reworked patches from SailorSat's CabMAME

   License     GPL-2.0+
   Copyright   2010-2016 - Chris Kennedy, Antonio Giner

 **************************************************************/

#ifndef __MODELINE_H__
#define __MODELINE_H__

#include "monitor.h"

//============================================================
//  CONSTANTS
//============================================================

// Modeline print flags
#define MS_LABEL      0x00000001
#define MS_LABEL_SDL  0x00000002
#define MS_PARAMS     0x00000004
#define MS_FULL       MS_LABEL | MS_PARAMS

// Modeline result   
#define R_V_FREQ_OFF    0x00000001
#define R_RES_STRETCH   0x00000002
#define R_OUT_OF_RANGE  0x00000004

// Modeline commands
#define MODELINE_DELETE      0x001
#define MODELINE_CREATE      0x002
#define MODELINE_UPDATE      0x004
#define MODELINE_UPDATE_LIST 0x008 

// Mode types  
#define MODE_OK         0x00000000
#define MODE_DESKTOP    0x10000000
#define MODE_ROTATED    0x20000000
#define MODE_DISABLED   0x40000000
#define MODE_USER_DEF   0x80000000
#define V_FREQ_EDITABLE 0x00000001
#define X_RES_EDITABLE  0x00000002
#define Y_RES_EDITABLE  0x00000004
#define XYV_EDITABLE   (X_RES_EDITABLE | Y_RES_EDITABLE | V_FREQ_EDITABLE )

#define DUMMY_WIDTH 1234
#define MAX_MODELINES 256

//============================================================
//  TYPE DEFINITIONS
//============================================================

typedef struct mode_result
{
	int    weight;
	int    x_scale;
	int    y_scale;
	int    v_scale;
	float  x_diff;
	float  y_diff;
	float  v_diff;
	float  x_ratio;
	float  y_ratio;
	float  v_ratio;
	bool   rotated;
} mode_result;

typedef struct modeline
{
	uint64_t    pclock;
	int    hactive;
	int    hbegin;
	int    hend;
	int    htotal;
	int    vactive;
	int    vbegin;
	int    vend;
	int    vtotal;
	int    interlace;
	int    doublescan;
	int    hsync;
	int    vsync;
	//
	double vfreq;
	double hfreq;
	//
	int    width;
	int    height;
	int    refresh;
	int    refresh_label;
	//
	int    type;
	int    range;
	//
	mode_result result;
} modeline;

//============================================================
//  PROTOTYPES
//============================================================

int modeline_create(modeline *s_mode, modeline *t_mode, monitor_range *range, config_settings *cs);
int modeline_compare(modeline *t_mode, modeline *best_mode);
char * modeline_print(modeline *mode, char *modeline, int flags);
char * modeline_result(modeline *mode, char *result);
int modeline_vesa_gtf(modeline *m);
int modeline_parse(const char *user_modeline, modeline *mode);
int modeline_to_monitor_range(monitor_range *range, modeline *mode);

#endif
