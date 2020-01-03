/**************************************************************
 
	 custom_video_powerstrip.h - PowerStrip interface routines
	 
	 ---------------------------------------------------------
 
	 SwitchRes   Modeline generation engine for emulation

	 License     GPL-2.0+
	 Copyright   2010-2016 - Chris Kennedy, Antonio Giner

 **************************************************************/

#include "custom_video.h"

//============================================================
//  TYPE DEFINITIONS
//============================================================

typedef struct
{
	int HorizontalActivePixels;
	int HorizontalFrontPorch;
	int HorizontalSyncWidth;
	int HorizontalBackPorch;
	int VerticalActivePixels;
	int VerticalFrontPorch;
	int VerticalSyncWidth;
	int VerticalBackPorch;
	int PixelClockInKiloHertz;
	union
	{
		int w;
		struct
		{
			unsigned :1;
			unsigned HorizontalPolarityNegative:1;
			unsigned VerticalPolarityNegative:1;
			unsigned :29;
		} b;
	} TimingFlags;
} MonitorTiming;

//============================================================
//  PROTOTYPES
//============================================================

int ps_init(int monitor_index, modeline *modeline);
int ps_reset(int monitor_index);
int ps_get_modeline(int monitor_index, modeline *modeline);
int ps_set_modeline(int monitor_index, modeline *modeline);
int ps_get_monitor_timing(int monitor_index, MonitorTiming *timing);
int ps_set_monitor_timing(int monitor_index, MonitorTiming *timing);
int ps_set_monitor_timing_string(int monitor_index, char *in);
int ps_set_refresh(int monitor_index, double vfreq);
int ps_best_pclock(int monitor_index, MonitorTiming *timing, int desired_pclock);
int ps_create_resolution(int monitor_index, modeline *modeline);
bool ps_read_timing_string(char *in, MonitorTiming *timing);
void ps_fill_timing_string(char *out, MonitorTiming *timing);
int ps_modeline_to_pstiming(modeline *modeline, MonitorTiming *timing);
int ps_pstiming_to_modeline(MonitorTiming *timing, modeline *modeline);
int ps_monitor_index (const char *display_name);


class pstrip_timing : public custom_video
{
	public:
		pstrip_timing(char *device_name, modeline *user_mode, char *ps_timing);
};
