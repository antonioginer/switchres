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

int xrandr_timing::xerrors = 0;

static int error_handler(Display *dpy, XErrorEvent *err)
{
	xrandr_timing::xerrors++;
	log_error("Display is set %d error code %d total error %d\n",dpy!=NULL, err->error_code, xrandr_timing::xerrors);
	return 0;
}

//============================================================
//  xrandr_timing::xrandr_timing
//============================================================

xrandr_timing::xrandr_timing(char *device_name, char *param)
{
	// Get current resolution
	int screen = -1;

	log_verbose("XRANDR: creation (%s,%s)\n", device_name, param);
	strncpy(m_device_name, device_name, sizeof(device_name) -1); // possible buffer aize overflow
	if ( param != NULL ) {
		strncpy(m_param, param, sizeof(param) -1);
	}

	// dpy is global to reduce open/close calls, resource is freed when modeline is reset
	dpy = XOpenDisplay(NULL);
	int major_version, minor_version;
	XRRQueryVersion(dpy, &major_version, &minor_version);
	log_verbose("XRANDR: version %d.%d\n",major_version,minor_version);

	// select current display and root window
	// root is global to reduce open/close calls, resource is freed when modeline is reset
	screen = DefaultScreen(dpy); // multiple screen ScreenCount (dpy)
	root = RootWindow(dpy, screen);

	video_modes_position = 0;
}
//============================================================
//  xrandr_timing::~xrandr_timing
//============================================================

xrandr_timing::~xrandr_timing()
{
	if ( dpy != NULL )
		XCloseDisplay(dpy);
}

//============================================================
//  xrandr_timing::init
//============================================================

bool xrandr_timing::init()
{
	XRRScreenResources *res = XRRGetScreenResourcesCurrent(dpy, root);

	// get screen size, rate and rotation from screen configuration
	XRRScreenConfiguration *sc = XRRGetScreenInfo(dpy, root);
	original_rate = XRRConfigCurrentRate(sc);
	original_size_id = XRRConfigCurrentConfiguration(sc, &original_rotation);
	XRRFreeScreenConfigInfo(sc);

	Rotation current_rotation = 0;
	for (int o = 0; o < res->noutput && !gmoutput_mode; o++)
	{
		XRROutputInfo *output_info = XRRGetOutputInfo (dpy, res, res->outputs[o]);
		if (!output_info)
			log_error("XRANDR: error could not get output 0x%x information\n", (uint) res->outputs[o]);

		// first connected output
		if (output_info->connection == RR_Connected)
		{
			for (int j = 0; j < output_info->nmode && !gmoutput_mode; j++)
			{
				if ( output_info->crtc )
				{
					XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(dpy, res, output_info->crtc);
					current_rotation = crtc_info->rotation;
					if (!strcmp(m_device_name, "auto") || !strcmp(m_device_name,output_info->name))
					{
						// connector name is kept but not necessary due to global gmoutput_primary varial, optimization can happen here
						log_verbose("XRANDR: found output connector '%s'\n", output_info->name);
						gmoutput_primary = o;
						gmoutput_total++;
					}
					for (int m = 0; m < res->nmode && !gmoutput_mode; m++)
					{
						XRRModeInfo *mode = &res->modes[m];
						// get screen mode
						if (crtc_info->mode == mode->id)
						{
							gmoutput_mode = mode->id;
							width = crtc_info->x + crtc_info->width;
							height = crtc_info->y + crtc_info->height;
						}
					}
				}
				if (current_rotation & 0xe) // screen rotation is left or right
				{
					log_verbose("XRANDR: desktop rotation is %s\n",(current_rotation & 0x2)?"left":((current_rotation & 0x8)?"right":"inverted"));
					// TODO set flag for desktop_rotated;
				}
			}
		}
		XRRFreeOutputInfo(output_info);
	}
	XRRFreeScreenResources(res);

	//handle no screen detected case
	if (gmoutput_total == 0)
	{
		log_error("XRANDR: error, no screen detected\n");
		return false;
	}

	return true;
}

//============================================================
//  xrandr_timing::modeline_reset
//============================================================

bool xrandr_timing::reset_mode()
{
	// Restore desktop resolution
	XRRScreenResources *res = XRRGetScreenResourcesCurrent(dpy, root);
	XRRScreenConfiguration *sc = XRRGetScreenInfo(dpy, root);

	XRRSetScreenConfigAndRate(dpy, sc, root, original_size_id, original_rotation, original_rate, CurrentTime);
	XRRFreeScreenConfigInfo(sc);
	XRRFreeScreenResources(res);

	log_verbose("XRANDR: original video mode restored\n");

	return true;
}

//============================================================
//  xrandr_timing::add_video_xrandr_mode
//============================================================

bool xrandr_timing::add_mode(modeline *mode)
{
	if (!mode)
		return false;

	// Add modeline to interface
	char name[48];
	sprintf(name,"GM-%dx%d_%.6f",mode->hactive, mode->vactive, mode->vfreq); // add ID

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

	// Create the modeline
	XSync(dpy, False);
	xerrors = 0;
	old_error_handler = XSetErrorHandler(error_handler);
	RRMode gmid = XRRCreateMode(dpy, root, &xmode);
	XSync(dpy, False);
	XSetErrorHandler(old_error_handler);
	if (xerrors)
		log_error("XRANDR: error in %s\n","XRRCreateMode");

	// Add new modeline to primary output
	XRRScreenResources *res = XRRGetScreenResourcesCurrent(dpy, root);

	XSync(dpy, False);
	xerrors = 0;
	old_error_handler = XSetErrorHandler(error_handler);
	XRRAddOutputMode(dpy, res->outputs[gmoutput_primary], gmid);
	XSync(dpy, False);
	XSetErrorHandler(old_error_handler);
	if (xerrors)
		log_error("XRANDR: error in %s\n","XRRAddOutputMode");

	XRRFreeScreenResources(res);

	return true;
}

//============================================================
//  xrandr_timing::set_video_xrandr_mode
//============================================================

bool xrandr_timing::set_mode(modeline *mode)
{
	// Use xrandr to switch to new mode.
	char name[48];
	sprintf(name,"GM-%dx%d_%.6f",mode->hactive, mode->vactive, mode->vfreq); // add ID

	XRRScreenResources *res = XRRGetScreenResourcesCurrent(dpy, root);
	XRROutputInfo *output_info = XRRGetOutputInfo(dpy, res, res->outputs[gmoutput_primary]);
	XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(dpy, res, output_info->crtc);

	// Select corresponding mode from modeline, can be enhanced by saving mode index to modeline structure
	XRRModeInfo *xmode=0;
	for (int m = 0; m < res->nmode; m++)
	{
		XRRModeInfo *tmp_mode = &res->modes[m];
		if (!strcmp(name, tmp_mode->name))
		{
			xmode = &res->modes[m];
		}
	}

	// Grab X server to prevent unwanted interaction from the window manager
	XGrabServer(dpy);

	// Disable all CRTCs
	for (int i = 0; i < output_info->ncrtc; i++)
	{
		if (XRRSetCrtcConfig(dpy, res, output_info->crtcs[i], CurrentTime, 0, 0, None, RR_Rotate_0, NULL, 0) != RRSetConfigSuccess)
			log_error("XRANDR: error when disabling CRTC\n");
	}
	log_verbose("XRANDR: CRTC %d: mode %#lx, %ux%u+%d+%d.\n", 0, crtc_info->mode,crtc_info->width, crtc_info->height, crtc_info->x, crtc_info->y);

	// Check if framebuffer size is correct
	int change_resolution = 0;
	if (width < crtc_info->x + mode->hactive)
	{
		width = crtc_info->x + mode->hactive;
		change_resolution = 1;
	}
	if (height < crtc_info->y + mode->vactive)
	{
		height = crtc_info->y + mode->vactive;
		change_resolution = 1;
	}

	// Enlarge the screen size for the new mode
	if (change_resolution)
	{
		log_verbose("XRANDR: change screen size\n");
		XSync(dpy, False);
		xerrors = 0;
		old_error_handler = XSetErrorHandler(error_handler);
		XRRSetScreenSize(dpy, root, width, height, (25.4 * width) / 96.0, (25.4 * height) / 96.0);
		XSync(dpy, False);
		XSetErrorHandler(old_error_handler);
		if (xerrors)
			log_error("XRANDR: error in %s\n","XRRSetScreenSize");
	}

	// Switch to new modeline
	XSync(dpy, False);
	xerrors = 0;
	old_error_handler = XSetErrorHandler(error_handler);
	XRRSetCrtcConfig(dpy, res, output_info->crtc, CurrentTime , crtc_info->x, crtc_info->y, xmode->id, original_rotation, crtc_info->outputs, crtc_info->noutput);
	XSync(dpy, False);
	XSetErrorHandler(old_error_handler);

	XRRFreeCrtcInfo(crtc_info);

	if (xerrors)
		log_error("XRANDR: error in %s\n","XRRSetCrtcConfig");

	// Release X server, events can be processed now
	XUngrabServer(dpy);

	crtc_info = XRRGetCrtcInfo(dpy, res, output_info->crtc); // recall crtc to settle parameters

	// If the crtc config modeline change fails, revert to original mode (prevents ending with black screen due to all crtc disabled)
	if (crtc_info->mode == 0)
	{
		log_error("XRANDR: error switching resolution, original mode restored\n");
		XRRScreenConfiguration *sc = XRRGetScreenInfo(dpy, root);
		XRRSetScreenConfigAndRate(dpy, sc, root, original_size_id, original_rotation, original_rate, CurrentTime);
		XRRFreeScreenConfigInfo(sc);
	}

	// check, verify current active mode
	for (int m = 0; m < res->nmode; m++)
	{
		XRRModeInfo *mode = &res->modes[m];
		if (mode->id == crtc_info->mode)
		log_verbose("XRANDR: mode %d id 0x%04x name %s clock %6.6fMHz\n", m, (int)mode->id, mode->name, (double)mode->dotClock / 1000000.0);
	}

	XRRFreeCrtcInfo(crtc_info);
	XRRFreeOutputInfo(output_info);
	XRRFreeScreenResources(res);

	return true;
}

//============================================================
//  xrandr_timing::del_video_xrandr_mode
//============================================================

bool xrandr_timing::delete_mode(modeline *mode)
{
	if (!mode)
		return false;

	char name[48];
	sprintf(name,"GM-%dx%d_%.6f",mode->hactive, mode->vactive, mode->vfreq); // add ID

	XRRScreenResources *res = XRRGetScreenResourcesCurrent (dpy, root);

	// Delete modeline
	for (int m = 0; m < res->nmode; m++)
	{
		XRRModeInfo *xmode = &res->modes[m];
		if (!strcmp(name, xmode->name))
		{
			XSync(dpy, False);
			xerrors = 0;
			old_error_handler = XSetErrorHandler(error_handler);
			XRRDeleteOutputMode (dpy, res->outputs[gmoutput_primary], xmode->id);
			if (xerrors)
				log_error("XRANDR: error in %s\n","XRRDeleteOutputMode");

			xerrors = 0;
			XRRDestroyMode (dpy, xmode->id);
			XSync(dpy, False);
			XSetErrorHandler(old_error_handler);
			if (xerrors)
				log_error("XRANDR: error in %s\n","XRRDestroyMode");
		}
	}

	XRRFreeScreenResources(res);

	return true;
}

//============================================================
//  pstrip_timing::set_timing
//============================================================

bool xrandr_timing::set_timing(modeline *mode)
{
	if ( mode->type & MODE_DESKTOP )
		return reset_mode();

	return set_mode(mode);
}
//============================================================
//  pstrip_timing::get_timing
//============================================================

bool xrandr_timing::get_timing(modeline *mode)
{
	XRRScreenResources *res = XRRGetScreenResourcesCurrent(dpy, root);

	if (video_modes_position < res->nmode)
	{
	
		XRRModeInfo *xmode = &res->modes[video_modes_position++];

		log_verbose("XRANDR: pos %d mode id 0x%04x name %s clock %6.6fMHz\n", video_modes_position, (int)xmode->id, xmode->name, (double)xmode->dotClock / 1000000.0);

		//xmode.name       
		//xmode.nameLength 

//		modeline mode;
//		memset(&mode, 0, sizeof(struct modeline));

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

		mode->width	= xmode->width;
		mode->height	= xmode->height;
		//mode->refresh_label = ???
		
		mode->type |= CUSTOM_VIDEO_TIMING_XRANDR;

		if (gmoutput_mode == (int)xmode->id)
			mode->type |= MODE_DESKTOP;
	}

	XRRFreeScreenResources(res);

	return true;
}

