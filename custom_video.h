/**************************************************************

   custom_video.h - Custom video library header

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   GroovyMAME  Integration of SwitchRes into the MAME project
               Some reworked patches from SailorSat's CabMAME

   License     GPL-2.0+
   Copyright   2010-2016 - Chris Kennedy, Antonio Giner

 **************************************************************/

#define CUSTOM_VIDEO_TIMING_MASK        0x00000ff0
#define CUSTOM_VIDEO_TIMING_SYSTEM      0x00000010
#define CUSTOM_VIDEO_TIMING_XRANDR      0x00000020
#define CUSTOM_VIDEO_TIMING_POWERSTRIP  0x00000040
#define CUSTOM_VIDEO_TIMING_ATI_LEGACY  0x00000080
#define CUSTOM_VIDEO_TIMING_ATI_ADL     0x00000100  

bool custom_video_init(char *device_name, char *device_id, modeline *desktop_mode, modeline *user_mode, modeline *mode_table, int method, char *s_param);
void custom_video_close();
bool custom_video_get_timing(modeline *mode);
bool custom_video_set_timing(modeline *mode);
bool custom_video_restore_timing();
bool custom_video_update_timing(modeline *mode);
int custom_video_parse_pci_id(char *device_id, int *vendor, int *device);
modeline *custom_video_get_backup_mode();
