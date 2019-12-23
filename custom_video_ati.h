/**************************************************************

	custom_video_ati.h - ATI legacy library header

	---------------------------------------------------------

	SwitchRes	Modeline generation engine for emulation

	GroovyMAME	Integration of SwitchRes into the MAME project
				Some reworked patches from SailorSat's CabMAME

	License     GPL-2.0+
	Copyright   2010-2016 - Chris Kennedy, Antonio Giner

 **************************************************************/

bool ati_init(char *device_name, char *device_key, char *device_id);
bool ati_get_modeline(modeline *mode);
bool ati_set_modeline(modeline *mode);
void ati_refresh_timings(void);
