/**************************************************************

   video_xrandr_xrandr.cpp - Linux XRANDR video management layer

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2020 - Chris Kennedy, Antonio Giner, Alexandre Wodarczyk

 **************************************************************/

#include <stdio.h>
#include "custom_video_xrandr.h"
#include "log.h"

//============================================================
//  error_handler 
//  xorg error handler
//============================================================

int xrandr_timing::m_xerrors = 0;
int xrandr_timing::m_xerrors_flag = 0;
int (*old_error_handler)(Display *, XErrorEvent *);

static int error_handler(Display *dpy, XErrorEvent *err)
{
	char buf[64];
	XGetErrorText(dpy, err->error_code, buf, 64);
	xrandr_timing::m_xerrors|=xrandr_timing::m_xerrors_flag;
	log_error("XRANDR: (error_handler) [ERROR] %s error code %d flags %02x\n", buf, err->error_code, xrandr_timing::m_xerrors);
	return 0;
}

//============================================================
//  xrandr_timing::xrandr_timing
//============================================================

xrandr_timing::xrandr_timing(char *device_name, char *param)
{
	log_verbose("XRANDR: (xrandr_timing) creation (%s,%s)\n", device_name, param);
	// Copy screen device name and limit size
	if ((strlen(device_name)+1) > 32)
	{
		strncpy(m_device_name, device_name, 31);
		log_error("XRANDR: (xrandr_timing) [ERROR] the devine name is too long it has been trucated to %s\n",m_device_name);
	} else {
		strcpy(m_device_name, device_name);
	}

	// m_dpy is global to reduce open/close calls, resource is freed when class is destroyed
	m_dpy = XOpenDisplay(NULL);

	// Display XRANDR version
	int major_version, minor_version;
	XRRQueryVersion(m_dpy, &major_version, &minor_version);
	log_verbose("XRANDR: (xrandr_timing) version %d.%d\n",major_version,minor_version);
	// was used by restore_mode XRRQueryExtension(m_dpy, &m_event_base, &m_error_base); 
}
//============================================================
//  xrandr_timing::~xrandr_timing
//============================================================

xrandr_timing::~xrandr_timing()
{
	// Free the display
	if (m_dpy != NULL)
		XCloseDisplay(m_dpy);
}

//============================================================
//  xrandr_timing::init
//============================================================

bool xrandr_timing::init()
{
	// Select current display and root window
	// display and root window are global variable to reduce open/close calls
	
	// screen_pos defines screen position, 0 is default and equivalent to 'auto'
	int screen_pos = -1; // first screen starts at 0

	bool detected = false;
	
	// Handle the screen name, "auto", "screen[0-9]" and XRANDR device name
	if (strlen(m_device_name) == 7 && !strncmp(m_device_name,"screen",6) && m_device_name[6]>='0' && m_device_name[6]<='9')
	{
		screen_pos = m_device_name[6]-'0';
		log_verbose("XRANDR: (init) check for screen number %d\n", screen_pos);
	} 

	for (int scr = 0;!detected && scr < ScreenCount(m_dpy);scr++)
	{
		m_root = RootWindow(m_dpy, scr);
		
		XRRScreenResources *res = XRRGetScreenResourcesCurrent(m_dpy, m_root);

		// Get default screen rotation from screen configuration
		XRRScreenConfiguration *sc = XRRGetScreenInfo(m_dpy, m_root);
		XRRConfigCurrentConfiguration(sc, &m_desktop_rotation);
		XRRFreeScreenConfigInfo(sc);

		Rotation current_rotation = 0;
		int output_position = 0;
		for (int o = 0;o < res->noutput;o++)
		{
			XRROutputInfo *output_info = XRRGetOutputInfo(m_dpy, res, res->outputs[o]);
			if (!output_info)
				log_error("XRANDR: (detect_connector) [ERROR] could not get output 0x%x information\n", (uint) res->outputs[o]);

			// Check all connected output
			if (output_info->connection == RR_Connected)
			{
				log_verbose("XRANDR: (detect_connector) check output connector '%s'\n", output_info->name);
				for (int j = 0;j < output_info->nmode;j++)
				{
					// If output has a crtc, select it
					if (output_info->crtc && m_desktop_output == -1)
					{
						XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(m_dpy, res, output_info->crtc);
						current_rotation = crtc_info->rotation;
						if (!strcmp(m_device_name, "auto") || !strcmp(m_device_name,output_info->name) || output_position == screen_pos)
						{
							log_verbose("XRANDR: (detect_connector) name '%s' id %d selected as primary output\n", output_info->name, o);
							// Save the output connector
							m_desktop_output = o;
	
							// identify the initial modeline id
							for (int m = 0;m < res->nmode && m_desktop_mode.id == 0;m++)
							{
								XRRModeInfo *mode = &res->modes[m];
								// Get screen mode
								if (crtc_info->mode == mode->id)
								{
									m_desktop_mode = *mode;
								}
							}
						}
						XRRFreeCrtcInfo(crtc_info);
					}
					if (current_rotation & 0xe) // Screen rotation is left or right
					{
						m_crtc_flags = MODE_ROTATED;
						log_verbose("XRANDR: (detect_connector) desktop rotation is %s\n",(current_rotation & 0x2)?"left":((current_rotation & 0x8)?"right":"inverted"));
					}
				}
				output_position++;
			}
			XRRFreeOutputInfo(output_info);
		}
		XRRFreeScreenResources(res);

		// set if screen is detected
		detected = m_desktop_output != -1;
	}

	// Handle no screen detected case
	if(!detected)
		log_error("XRANDR: (init) [ERROR] no screen detected\n");

	return detected;
}

//============================================================
//  xrandr_timing::update_mode
//============================================================

bool xrandr_timing::update_mode(modeline *mode)
{
	if (!mode)
		return false;

	// Handle no screen detected case
	if (m_desktop_output == -1)
	{
		log_error("XRANDR: (update_mode) [ERROR] no screen detected\n");
		return false;
	}

	if (!delete_mode(mode))
	{
		log_error("XRANDR: (update_mode) [ERROR] delete operation not successful");
		return false;
	}

	if (!add_mode(mode))
	{
		log_error("XRANDR: (update_mode) [ERROR] add operation not successful");
		return false;
	}

	return true;
}
//============================================================
//  xrandr_timing::add_mode
//============================================================

bool xrandr_timing::add_mode(modeline *mode)
{
	if (!mode)
		return false;

	// Handle no screen detected case
	if (m_desktop_output == -1)
	{
		log_error("XRANDR: (add_mode) [ERROR] no screen detected\n");
		return false;
	}

	XRRModeInfo *pmode = find_mode(mode);
	if (pmode != NULL)
	{
		log_error("XRANDR: (add_mode) [ERROR] mode already exist\n");
	}

	// Add modeline to interface
	char name[48];
	sprintf(name,"SR-%dx%d_%f",mode->hactive, mode->vactive, mode->vfreq); // Add ID

	// Setup the xrandr mode structure
	XRRModeInfo xmode;
	xmode.name       = name;
	xmode.nameLength = strlen(name);
	xmode.dotClock   = float(mode->pclock);
	xmode.width      = mode->hactive;
	xmode.hSyncStart = mode->hbegin;
	xmode.hSyncEnd   = mode->hend;
	xmode.hTotal     = mode->htotal;
	xmode.height     = mode->vactive;
	xmode.vSyncStart = mode->vbegin;
	xmode.vSyncEnd   = mode->vend;
	xmode.vTotal     = mode->vtotal;
	xmode.modeFlags  = (mode->interlace?RR_Interlace:0) | (mode->doublescan?RR_DoubleScan:0) | (mode->hsync?RR_HSyncPositive:RR_HSyncNegative) | (mode->vsync?RR_VSyncPositive:RR_VSyncNegative);
		
	mode->type |= CUSTOM_VIDEO_TIMING_XRANDR;

	// Create the modeline
	XSync(m_dpy, False);
	m_xerrors = 0;
	m_xerrors_flag = 0x01;
	old_error_handler = XSetErrorHandler(error_handler);
	RRMode gmid = XRRCreateMode(m_dpy, m_root, &xmode);
	XSync(m_dpy, False);
	XSetErrorHandler(old_error_handler);
	if (m_xerrors & m_xerrors_flag)
	{
		log_error("XRANDR: (add_mode) [ERROR] in %s\n","XRRCreateMode");
		return false;
	} 
	else 
	{
		mode->platform_data = gmid;
	}

	// Add new modeline to primary output
	XRRScreenResources *res = XRRGetScreenResourcesCurrent(m_dpy, m_root);

	XSync(m_dpy, False);
	m_xerrors_flag = 0x02;
	old_error_handler = XSetErrorHandler(error_handler);
	XRRAddOutputMode(m_dpy, res->outputs[m_desktop_output], gmid);
	XSync(m_dpy, False);
	XSetErrorHandler(old_error_handler);

	XRRFreeScreenResources(res);

	if (m_xerrors & m_xerrors_flag)
	{
		log_error("XRANDR: (add_mode) [ERROR] in %s\n","XRRAddOutputMode");
		// remove unlinked modeline
		XRRDestroyMode(m_dpy, pmode->id);
	}

	return m_xerrors==0;
}

//============================================================
//  xrandr_timing::find_mode
//============================================================

XRRModeInfo *xrandr_timing::find_mode(modeline *mode)
{
	XRRScreenResources *res = XRRGetScreenResourcesCurrent(m_dpy, m_root);

	// Select corresponding mode from modeline, can be enhanced by saving mode index to modeline structure
	XRRModeInfo *pmode=NULL;

	// if name search is not successful, try with a parameter search instead
	for (int m = 0;m < res->nmode && !pmode;m++)
	{
		XRRModeInfo *xmode = &res->modes[m];
		if (mode->platform_data == xmode->id)
		{
			pmode = &res->modes[m];
		}
	}

	XRRFreeScreenResources(res);

	return pmode;
}

//============================================================
//  xrandr_timing::set_timing
//============================================================

bool xrandr_timing::set_timing(modeline *mode)
{
	// Handle no screen detected case
	if (m_desktop_output == -1)
	{
		log_error("XRANDR: (set_timing) [ERROR] no screen detected\n");
		return false;
	}

	XRRModeInfo *pmode = NULL;
	
	if (mode->type & MODE_DESKTOP)
	{
		pmode = &m_desktop_mode;
	} else {
		pmode = find_mode(mode);
	}

	if (pmode == NULL)
	{
		log_error("XRANDR: (set_timing) [ERROR] mode not found\n");
		return false;
	}

	// Use xrandr to switch to new mode.
	XRRScreenResources *res = XRRGetScreenResourcesCurrent(m_dpy, m_root);
	XRROutputInfo *output_info = XRRGetOutputInfo(m_dpy, res, res->outputs[m_desktop_output]);
	XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(m_dpy, res, output_info->crtc);

	if (pmode->id == crtc_info->mode)
	{
		log_verbose("XRANDR: (set_timing) modeline is already active\n");
		XRRFreeCrtcInfo(crtc_info);
		XRRFreeOutputInfo(output_info);
		XRRFreeScreenResources(res);
		return true;
	}

	m_xerrors = 0;

	// Grab X server to prevent unwanted interaction from the window manager
	XGrabServer(m_dpy);

	// Disable CRTC
	if (XRRSetCrtcConfig(m_dpy, res, output_info->crtc, CurrentTime, crtc_info->x, crtc_info->y, None, RR_Rotate_0, NULL, 0) != RRSetConfigSuccess)
	{
		log_error("XRANDR: (set_timing) [ERROR] when disabling CRTC\n");
		// Release X server, events can be processed now
		m_xerrors_flag = 0x01;
		m_xerrors |= m_xerrors_flag;
	}

	log_verbose("XRANDR: (set_timing) CRTC %d mode %#lx, %ux%u+%d+%d.\n", 0, crtc_info->mode, crtc_info->width, crtc_info->height, crtc_info->x, crtc_info->y);

        if (m_xerrors == 0)
	{
		log_verbose("XRANDR: (set_timing) change screen size to %d x %d\n", mode->hactive, mode->vactive);
		XSync(m_dpy, False);
		m_xerrors_flag = 0x02;
		old_error_handler = XSetErrorHandler(error_handler);
		XRRSetScreenSize(m_dpy, m_root, crtc_info->x + mode->hactive, crtc_info->y + mode->vactive, (25.4 * mode->hactive) / 96.0, (25.4 * mode->vactive) / 96.0);
		XSync(m_dpy, False);
		XSetErrorHandler(old_error_handler);
		if (m_xerrors & m_xerrors_flag)
		{
			log_error("XRANDR: (set_timing) [ERROR] in %s\n","XRRSetScreenSize");
		}
	}

	// Switch to new modeline
	XSync(m_dpy, False);
	m_xerrors_flag = 0x04;
	old_error_handler = XSetErrorHandler(error_handler);
	XRRSetCrtcConfig(m_dpy, res, output_info->crtc, CurrentTime, crtc_info->x, crtc_info->y, pmode->id, m_desktop_rotation, crtc_info->outputs, crtc_info->noutput);
	XSync(m_dpy, False);
	XSetErrorHandler(old_error_handler);

	// Release X server, events can be processed now
	XUngrabServer(m_dpy);

	XRRFreeCrtcInfo(crtc_info);

	if (m_xerrors & m_xerrors_flag)
	{
		log_error("XRANDR: (set_timing) [ERROR] in %s\n","XRRSetCrtcConfig");
	}

	crtc_info = XRRGetCrtcInfo(m_dpy, res, output_info->crtc); // Recall crtc to settle parameters

	// log crtc config modeline change fail 
	if (crtc_info->mode == 0)
	{
		log_error("XRANDR: (set_timing) [ERROR] switching resolution, no modeline\n");
	}

	// Verify current active mode
	for (int m = 0;m < res->nmode && crtc_info->mode;m++)
	{
		XRRModeInfo *mode = &res->modes[m];
		if (mode->id == crtc_info->mode)
		{
			log_verbose("XRANDR: (set_timing) pos %d id 0x%04lx name %s clock %6.6fMHz\n", m, mode->id, mode->name, (double)mode->dotClock / 1000000.0);
		}
	}

	XRRFreeCrtcInfo(crtc_info);
	XRRFreeOutputInfo(output_info);
	XRRFreeScreenResources(res);

	return m_xerrors==0;
}

//============================================================
//  xrandr_timing::delete_mode
//============================================================

bool xrandr_timing::delete_mode(modeline *mode)
{
	// Handle no screen detected case
	if (m_desktop_output == -1)
	{
		log_error("XRANDR: (delete_mode) [ERROR] no screen detected\n");
		return false;
	}

	if (!mode)
		return false;

	XRRScreenResources *res = XRRGetScreenResourcesCurrent(m_dpy, m_root);

	int total_xerrors = 0;
	// Delete modeline
	for (int m = 0;m<res->nmode;m++)
	{
		XRRModeInfo *xmode = &res->modes[m];
		if (mode->platform_data == xmode->id)
		{
			XRROutputInfo *output_info = XRRGetOutputInfo(m_dpy, res, res->outputs[m_desktop_output]);
			XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(m_dpy, res, output_info->crtc);
			if (xmode->id == crtc_info->mode)
			{
				log_error("XRANDR: (delete_mode) [WARNING] modeline is currently active\n");
			}

			XRRFreeCrtcInfo(crtc_info);
			XRRFreeOutputInfo(output_info);

			XSync(m_dpy, False);
			m_xerrors = 0;
			m_xerrors_flag = 0x01;
			old_error_handler = XSetErrorHandler(error_handler);
			XRRDeleteOutputMode(m_dpy, res->outputs[m_desktop_output], xmode->id);
			if (m_xerrors & m_xerrors_flag)
			{
				log_error("XRANDR: (delete_mode) [ERROR] in %s\n","XRRDeleteOutputMode");
				total_xerrors++;
			}

			m_xerrors_flag = 0x02;
			XRRDestroyMode(m_dpy, xmode->id);
			XSync(m_dpy, False);
			XSetErrorHandler(old_error_handler);
			if (m_xerrors & m_xerrors_flag)
			{
				log_error("XRANDR: (delete_mode) [ERROR] in %s\n","XRRDestroyMode");
				total_xerrors++;
			}
		}
	}

	XRRFreeScreenResources(res);

	return total_xerrors==0;
}

//============================================================
//  xrandr_timing::get_timing
//============================================================

bool xrandr_timing::get_timing(modeline *mode)
{
	// Handle no screen detected case
	if (m_desktop_output == -1)
	{
		log_error("XRANDR: (get_timing) [ERROR] no screen detected\n");
		return false;
	}

	XRRScreenResources *res = XRRGetScreenResourcesCurrent(m_dpy, m_root);
	XRROutputInfo *output_info = XRRGetOutputInfo(m_dpy, res, res->outputs[m_desktop_output]);

	// Cycle through the modelines and report them back to the display manager
	if (m_video_modes_position < output_info->nmode)
	{
		for (int m = 0;m < res->nmode;m++)
		{
			XRRModeInfo *xmode = &res->modes[m];

			if (xmode->id==output_info->modes[m_video_modes_position]) 
			{
				mode->platform_data = xmode->id;

				mode->pclock  	= xmode->dotClock;
				mode->hactive 	= xmode->width;
				mode->hbegin  	= xmode->hSyncStart;
				mode->hend    	= xmode->hSyncEnd;
				mode->htotal  	= xmode->hTotal;
				mode->vactive 	= xmode->height;
				mode->vbegin  	= xmode->vSyncStart;
				mode->vend    	= xmode->vSyncEnd;
				mode->vtotal  	= xmode->vTotal;
				mode->interlace = (xmode->modeFlags & RR_Interlace)?1:0;
				mode->doublescan = (xmode->modeFlags & RR_DoubleScan)?1:0;
				mode->hsync     = (xmode->modeFlags & RR_HSyncPositive)?1:0;
				mode->vsync     = (xmode->modeFlags & RR_VSyncPositive)?1:0;

				mode->hfreq 	= mode->pclock / mode->htotal;
				mode->vfreq 	= mode->hfreq / mode->vtotal * (mode->interlace?2:1);
				mode->refresh 	= mode->vfreq;

				mode->width	= xmode->width;
				mode->height	= xmode->height;
		
				mode->type |= m_crtc_flags; // Add the rotation flag from the crtc
				if (strncmp(xmode->name,"SR-",3) == 0) {
					log_verbose("XRANDR: (get_timing) [WARNING] modeline %s detected\n", xmode->name);
					mode->type |= CUSTOM_VIDEO_TIMING_XRANDR;
				} else {
					mode->type |= CUSTOM_VIDEO_TIMING_SYSTEM;
				}
		
				if (m_desktop_mode.id == xmode->id)
				{
					mode->type |= MODE_DESKTOP; // Add the desktop flag to original modeline
				}
			}
		} 
		m_video_modes_position++;
	} else {
		// Inititalise the position for the modeline list
		m_video_modes_position = 0;
	}

	XRRFreeOutputInfo(output_info);
	XRRFreeScreenResources(res);

	return true;
}

