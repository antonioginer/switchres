/**************************************************************

   display_linux.cpp - Display manager for Linux

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2016 - Chris Kennedy, Antonio Giner

 **************************************************************/

#include <stdio.h>
#include "display_linux.h"

const auto log_verbose = printf;
const auto log_error = printf;

//============================================================
//  windows_display::init
//============================================================

bool linux_display::init(display_settings *ds)
{
	return true;
}
