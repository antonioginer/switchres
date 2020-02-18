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

	// m_pdisplay is global to reduce open/close calls, resource is freed when class is destroyed
	m_pdisplay = XOpenDisplay(NULL);

	// Display XRANDR version
	int major_version, minor_version;
	XRRQueryVersion(m_pdisplay, &major_version, &minor_version);
	log_verbose("XRANDR: (xrandr_timing) version %d.%d\n",major_version,minor_version);
	// was used by restore_mode XRRQueryExtension(m_pdisplay, &m_event_base, &m_error_base); 
}
//============================================================
//  xrandr_timing::~xrandr_timing
//============================================================

xrandr_timing::~xrandr_timing()
{
	// Free the display
	if (m_pdisplay != NULL)
		XCloseDisplay(m_pdisplay);
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
		log_verbose("XRANDR: (init) check screen number %d\n", screen_pos);
	} 

	for (int screen = 0;!detected && screen < ScreenCount(m_pdisplay);screen++)
	{
		m_root = RootWindow(m_pdisplay, screen);
		
		XRRScreenResources *resources = XRRGetScreenResourcesCurrent(m_pdisplay, m_root);

		// Get default screen rotation from screen configuration
		XRRScreenConfiguration *sc = XRRGetScreenInfo(m_pdisplay, m_root);
		XRRConfigCurrentConfiguration(sc, &m_desktop_rotation);
		XRRFreeScreenConfigInfo(sc);

		Rotation current_rotation = 0;
		int output_position = 0;
		for (int o = 0;o < resources->noutput;o++)
		{
			XRROutputInfo *output_info = XRRGetOutputInfo(m_pdisplay, resources, resources->outputs[o]);
			if (!output_info)
				log_error("XRANDR: (detect_connector) [ERROR] could not get output 0x%x information\n", (uint) resources->outputs[o]);

			// Check all connected output
			if (output_info->connection == RR_Connected)
			{
				log_verbose("XRANDR: (detect_connector) check output connector '%s'\n", output_info->name);
				for (int j = 0;j < output_info->nmode;j++)
				{
					// If output has a crtc, select it
					if (output_info->crtc && m_desktop_output == -1)
					{
						XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(m_pdisplay, resources, output_info->crtc);
						current_rotation = crtc_info->rotation;
						if (!strcmp(m_device_name, "auto") || !strcmp(m_device_name,output_info->name) || output_position == screen_pos)
						{
							log_verbose("XRANDR: (detect_connector) name '%s' id %d selected as primary output\n", output_info->name, o);
							// Save the output connector
							m_desktop_output = o;
	
							// identify the initial modeline id
							for (int m = 0;m < resources->nmode && m_desktop_mode.id == 0;m++)
							{
								XRRModeInfo *pxmode = &resources->modes[m];
								// Get screen mode
								if (crtc_info->mode == pxmode->id)
								{
									m_desktop_mode = *pxmode;
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
		XRRFreeScreenResources(resources);

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

	if (find_mode(mode) != NULL)
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
	XSync(m_pdisplay, False);
	m_xerrors = 0;
	m_xerrors_flag = 0x01;
	old_error_handler = XSetErrorHandler(error_handler);
	RRMode gmid = XRRCreateMode(m_pdisplay, m_root, &xmode);
	XSync(m_pdisplay, False);
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
	XRRScreenResources *resources = XRRGetScreenResourcesCurrent(m_pdisplay, m_root);

	XSync(m_pdisplay, False);
	m_xerrors_flag = 0x02;
	old_error_handler = XSetErrorHandler(error_handler);
	XRRAddOutputMode(m_pdisplay, resources->outputs[m_desktop_output], gmid);
	XSync(m_pdisplay, False);
	XSetErrorHandler(old_error_handler);

	XRRFreeScreenResources(resources);

	if (m_xerrors & m_xerrors_flag)
	{
		log_error("XRANDR: (add_mode) [ERROR] in %s\n","XRRAddOutputMode");
		// remove unlinked modeline
		if (gmid) {
			log_error("XRANDR: (add_mode) [ERROR] remove mode [%04lx]\n", gmid);
			XRRDestroyMode(m_pdisplay, gmid);
		}
	}

	return m_xerrors==0;
}

//============================================================
//  xrandr_timing::find_mode
//============================================================

XRRModeInfo *xrandr_timing::find_mode(modeline *mode)
{
	XRRScreenResources *resources = XRRGetScreenResourcesCurrent(m_pdisplay, m_root);

	// Select corresponding mode from modeline, can be enhanced by saving mode index to modeline structure
	XRRModeInfo *pxmode=NULL;

	// if name search is not successful, try with a parameter search instead
	for (int m = 0;m < resources->nmode && !pxmode;m++)
	{
		XRRModeInfo *pxmode2 = &resources->modes[m];
		if (mode->platform_data == pxmode2->id)
		{
			pxmode = &resources->modes[m];
		}
	}

	XRRFreeScreenResources(resources);

	return pxmode;
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

	XRRModeInfo *pxmode = NULL;
	
	if (mode->type & MODE_DESKTOP)
	{
		pxmode = &m_desktop_mode;
	} else {
		pxmode = find_mode(mode);
	}

	if (pxmode == NULL)
	{
		log_error("XRANDR: (set_timing) [ERROR] mode not found\n");
		return false;
	}

	// Use xrandr to switch to new mode.
	XRRScreenResources *resources = XRRGetScreenResourcesCurrent(m_pdisplay, m_root);
	XRROutputInfo *output_info = XRRGetOutputInfo(m_pdisplay, resources, resources->outputs[m_desktop_output]);
	XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(m_pdisplay, resources, output_info->crtc);

	if (pxmode->id == crtc_info->mode)
	{
		log_verbose("XRANDR: (set_timing) mode [%04lx] is already active\n",pxmode->id);
		XRRFreeCrtcInfo(crtc_info);
		XRRFreeOutputInfo(output_info);
		XRRFreeScreenResources(resources);
		return true;
	}

	m_xerrors = 0;

	log_verbose("XRANDR: (set_timing) switching mode [%04lx] %ux%u+%d+%d --> [%04lx] %ux%u+%d+%d\n", crtc_info->mode, crtc_info->width, crtc_info->height, crtc_info->x, crtc_info->y, pxmode->id, pxmode->width, pxmode->height, crtc_info->x, crtc_info->y);

	// Grab X server to prevent unwanted interaction from the window manager
	XGrabServer(m_pdisplay);

	unsigned int width=0;
	unsigned int height=0;

	for (int c = 0; c < output_info->ncrtc; c++) {
		XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(m_pdisplay, resources, output_info->crtcs[c]);
		if ( output_info->crtcs[c] == output_info->crtc)
		{
			log_verbose("XRANDR: (set_timing) [DEBUG] <*> %d: %04lx %dx%d+%d+%d\n", c, crtc_info->mode, crtc_info->width, crtc_info->height, crtc_info->x, crtc_info->y);
			if (crtc_info->x + pxmode->width > width)
				width=crtc_info->x + pxmode->width;
			if (crtc_info->y + pxmode->height > height)
				height=crtc_info->y + pxmode->height;
		} else {
			log_verbose("XRANDR: (set_timing) [DEBUG] < > %d: %04lx %dx%d+%d+%d\n", c, crtc_info->mode, crtc_info->width, crtc_info->height, crtc_info->x, crtc_info->y);
			if (crtc_info->x + crtc_info->width > width)
				width=crtc_info->x + crtc_info->width;
			if (crtc_info->y + crtc_info->height > height)
				height=crtc_info->y + crtc_info->height;
		}
	}

	// Disable CRTC
	if (XRRSetCrtcConfig(m_pdisplay, resources, output_info->crtc, CurrentTime, 0, 0, None, RR_Rotate_0, NULL, 0) != RRSetConfigSuccess)
	{
		log_error("XRANDR: (set_timing) [ERROR] when disabling CRTC\n");
		m_xerrors_flag = 0x01;
		m_xerrors |= m_xerrors_flag;
	}

        if (m_xerrors == 0)
	{
		log_verbose("XRANDR: (set_timing) [DEBUG] changing screen size to %d x %d\n", width, height);
		XSync(m_pdisplay, False);
		m_xerrors_flag = 0x02;
		old_error_handler = XSetErrorHandler(error_handler);
		XRRSetScreenSize(m_pdisplay, m_root, width, height, (25.4 * mode->hactive) / 96.0, (25.4 * mode->vactive) / 96.0);
		XSync(m_pdisplay, False);
		XSetErrorHandler(old_error_handler);
		if (m_xerrors & m_xerrors_flag)
		{
			log_error("XRANDR: (set_timing) [ERROR] in %s\n","XRRSetScreenSize");
		}
	}

	// Switch to new modeline
	XSync(m_pdisplay, False);
	m_xerrors_flag = 0x04;
	old_error_handler = XSetErrorHandler(error_handler);
	XRRSetCrtcConfig(m_pdisplay, resources, output_info->crtc, CurrentTime, crtc_info->x, crtc_info->y, pxmode->id, m_desktop_rotation, crtc_info->outputs, crtc_info->noutput);
	XSync(m_pdisplay, False);
	XSetErrorHandler(old_error_handler);

	// Release X server, events can be processed now
	XUngrabServer(m_pdisplay);

	XRRFreeCrtcInfo(crtc_info);

	if (m_xerrors & m_xerrors_flag)
	{
		log_error("XRANDR: (set_timing) [ERROR] in %s\n","XRRSetCrtcConfig");
	}

	crtc_info = XRRGetCrtcInfo(m_pdisplay, resources, output_info->crtc); // Recall crtc to settle parameters

	// log crtc config modeline change fail 
	if (crtc_info->mode == 0)
	{
		log_error("XRANDR: (set_timing) [ERROR] switching resolution, no modeline\n");
	}

	// Verify current active mode
	for (int m = 0;m < resources->nmode && crtc_info->mode;m++)
	{
		XRRModeInfo *pxmode2 = &resources->modes[m];
		if (pxmode2->id == crtc_info->mode)
		{
			log_verbose("XRANDR: (set_timing) active mode [%04lx] name %s clock %6.6fMHz %ux%u+%d+%d\n", pxmode2->id, pxmode2->name, (double)pxmode2->dotClock / 1000000.0, pxmode->width, pxmode->height, crtc_info->x, crtc_info->y);
		}
	}

	XRRFreeCrtcInfo(crtc_info);
	XRRFreeOutputInfo(output_info);
	XRRFreeScreenResources(resources);

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

	XRRScreenResources *resources = XRRGetScreenResourcesCurrent(m_pdisplay, m_root);

	int total_xerrors = 0;
	// Delete modeline
	for (int m = 0;m<resources->nmode;m++)
	{
		XRRModeInfo *pxmode = &resources->modes[m];
		if (mode->platform_data == pxmode->id)
		{
			XRROutputInfo *output_info = XRRGetOutputInfo(m_pdisplay, resources, resources->outputs[m_desktop_output]);
			XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(m_pdisplay, resources, output_info->crtc);
			if (pxmode->id == crtc_info->mode)
			{
				log_error("XRANDR: (delete_mode) [WARNING] modeline [%04lx] is currently active\n", pxmode->id);
			}

			XRRFreeCrtcInfo(crtc_info);
			XRRFreeOutputInfo(output_info);

			XSync(m_pdisplay, False);
			m_xerrors = 0;
			m_xerrors_flag = 0x01;
			old_error_handler = XSetErrorHandler(error_handler);
			XRRDeleteOutputMode(m_pdisplay, resources->outputs[m_desktop_output], pxmode->id);
			if (m_xerrors & m_xerrors_flag)
			{
				log_error("XRANDR: (delete_mode) [ERROR] in %s\n","XRRDeleteOutputMode");
				total_xerrors++;
			}

			m_xerrors_flag = 0x02;
			XRRDestroyMode(m_pdisplay, pxmode->id);
			XSync(m_pdisplay, False);
			XSetErrorHandler(old_error_handler);
			if (m_xerrors & m_xerrors_flag)
			{
				log_error("XRANDR: (delete_mode) [ERROR] in %s\n","XRRDestroyMode");
				total_xerrors++;
			}
		}
	}

	XRRFreeScreenResources(resources);

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

	XRRScreenResources *resources = XRRGetScreenResourcesCurrent(m_pdisplay, m_root);
	XRROutputInfo *output_info = XRRGetOutputInfo(m_pdisplay, resources, resources->outputs[m_desktop_output]);

	// Cycle through the modelines and report them back to the display manager
	if (m_video_modes_position < output_info->nmode)
	{
		for (int m = 0;m < resources->nmode;m++)
		{
			XRRModeInfo *pxmode = &resources->modes[m];

			if (pxmode->id==output_info->modes[m_video_modes_position]) 
			{
				mode->platform_data = pxmode->id;

				mode->pclock  	= pxmode->dotClock;
				mode->hactive 	= pxmode->width;
				mode->hbegin  	= pxmode->hSyncStart;
				mode->hend    	= pxmode->hSyncEnd;
				mode->htotal  	= pxmode->hTotal;
				mode->vactive 	= pxmode->height;
				mode->vbegin  	= pxmode->vSyncStart;
				mode->vend    	= pxmode->vSyncEnd;
				mode->vtotal  	= pxmode->vTotal;
				mode->interlace = (pxmode->modeFlags & RR_Interlace)?1:0;
				mode->doublescan = (pxmode->modeFlags & RR_DoubleScan)?1:0;
				mode->hsync     = (pxmode->modeFlags & RR_HSyncPositive)?1:0;
				mode->vsync     = (pxmode->modeFlags & RR_VSyncPositive)?1:0;

				mode->hfreq 	= mode->pclock / mode->htotal;
				mode->vfreq 	= mode->hfreq / mode->vtotal * (mode->interlace?2:1);
				mode->refresh 	= mode->vfreq;

				mode->width	= pxmode->width;
				mode->height	= pxmode->height;
		
				mode->type |= m_crtc_flags; // Add the rotation flag from the crtc
				if (strncmp(pxmode->name,"SR-",3) == 0) {
					log_verbose("XRANDR: (get_timing) [WARNING] modeline %s detected\n", pxmode->name);
					mode->type |= CUSTOM_VIDEO_TIMING_XRANDR;
				} else {
					mode->type |= CUSTOM_VIDEO_TIMING_SYSTEM;
				}
		
				if (m_desktop_mode.id == pxmode->id)
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
	XRRFreeScreenResources(resources);

	return true;
}

