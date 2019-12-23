/**************************************************************

   switchres_proto.h - SwichRes prototypes header

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   GroovyMAME  Integration of SwitchRes into the MAME project
               Some reworked patches from SailorSat's CabMAME

   License     GPL-2.0+
   Copyright   2010-2016 - Chris Kennedy, Antonio Giner

 **************************************************************/

#ifndef __SWITCHRES_H_PROTO__
#define __SWITCHRES_H_PROTO__

//============================================================
//  PROTOTYPES
//============================================================

// util.cpp
int normalize(int a, int b);
int real_res(int x);

// switchres.cpp
bool switchres_get_video_mode(running_machine &machine);
int switchres_get_monitor_specs(running_machine &machine);
void switchres_init(running_machine &machine);
void switchres_get_game_info(running_machine &machine);
bool switchres_check_resolution_change(running_machine &machine);
void switchres_set_options(running_machine &machine);
bool effective_orientation(running_machine &machine);

// OSD - switchres.cpp
bool switchres_init_osd(running_machine &machine);
bool switchres_modeline_setup(running_machine &machine);
bool switchres_modeline_remove(running_machine &machine);

#endif
