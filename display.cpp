/**************************************************************

   display.cpp - Display manager

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2016 - Chris Kennedy, Antonio Giner

 **************************************************************/

#include "display.h"
#include "display_windows.h"

//============================================================
//  display_manager::make
//============================================================

display_manager *display_manager::make()
{

#if defined(_WIN32)
	m_display_manager = new windows_display();
#elif defined(__linux__)
	m_display_manager = new linux_display();
#endif

	if (m_display_manager)
	{
		return m_display_manager;
	}

	return nullptr;
}

