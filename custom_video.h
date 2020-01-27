/**************************************************************

   custom_video.h - Custom video library header

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2016 - Chris Kennedy, Antonio Giner

 **************************************************************/

#ifndef __CUSTOM_VIDEO__
#define __CUSTOM_VIDEO__


#include "modeline.h"

#define CUSTOM_VIDEO_TIMING_MASK        0x00000ff0
#define CUSTOM_VIDEO_TIMING_SYSTEM      0x00000010
#define CUSTOM_VIDEO_TIMING_XRANDR      0x00000020
#define CUSTOM_VIDEO_TIMING_POWERSTRIP  0x00000040
#define CUSTOM_VIDEO_TIMING_ATI_LEGACY  0x00000080
#define CUSTOM_VIDEO_TIMING_ATI_ADL     0x00000100

// Custom video caps
#define CUSTOM_VIDEO_CAPS_UPDATE      0x001
#define CUSTOM_VIDEO_CAPS_ADD         0x002

// Modeline commands
#define MODELINE_DELETE      0x001
#define MODELINE_CREATE      0x002
#define MODELINE_UPDATE      0x004
#define MODELINE_UPDATE_LIST 0x008 


class custom_video
{
public:

	custom_video();
	virtual ~custom_video();

	custom_video *make(char *device_name, char *device_id, modeline *user_mode, int method, char *s_param);
	virtual const char *api_name() { return "empty"; }
	virtual bool init();
	virtual void close();
	virtual int caps() { return 0; }
	
	virtual bool add_mode(modeline *mode);
	virtual bool update_mode(modeline *mode);

	virtual bool get_timing(modeline *mode);
	virtual bool set_timing(modeline *mode);


	modeline m_user_mode = {};
	modeline m_backup_mode = {};

	char ps_timing[256];

private:
	char m_device_name[32];
	char m_device_key[128];
	
	custom_video *m_custom_video = 0;
	int m_custom_method;

};

#endif
