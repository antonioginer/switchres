/**************************************************************

   display_linux.h - Display manager for Linux

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2020 - Chris Kennedy, Antonio Giner, Alexandre Wodarczyk

 **************************************************************/

// X11 Xrandr headers
#include <X11/extensions/Xrandr.h>

#include <string.h>

#include "display.h"

class linux_display : public display_manager
{
	public:
		linux_display() {};
		~linux_display() {};
		bool init(display_settings *ds);

	private:
};
