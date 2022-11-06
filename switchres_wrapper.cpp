/**************************************************************

   switchres_wrapper.cpp - Switchres C wrapper API

   ---------------------------------------------------------

   Switchres   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2022 Chris Kennedy, Antonio Giner,
                         Alexandre Wodarczyk, Gil Delescluse

 **************************************************************/

#define MODULE_API_EXPORTS
#include "switchres.h"
#include "switchres_wrapper.h"
#include "log.h"
#include <stdio.h>
#include <locale>
#ifdef __cplusplus
extern "C" {
#endif

#define SR_ACTION_GET         1<<0
#define SR_ACTION_GET_FROM_ID 1<<1
#define SR_ACTION_FLUSH       1<<2
#define SR_ACTION_SWITCH      1<<3


//============================================================
//  PROTOTYPES
//============================================================

int sr_mode_internal(int width, int height, double refresh, int interlace, sr_mode *srm, int action, const char *caller);
void modeline_to_sr_mode(modeline* m, sr_mode* srm);


//============================================================
//  GLOBALS
//============================================================

// Switchres manager object
switchres_manager* swr;

// Multi-monitor helper
static int disp_index = 0;
display_manager *sr_disp() { return swr->display(disp_index); }


//============================================================
// Start of Switchres API
//============================================================

//============================================================
//  sr_init
//============================================================

MODULE_API void sr_init()
{
	setlocale(LC_NUMERIC, "C");
	swr = new switchres_manager;
	swr->parse_config("switchres.ini");
}


//============================================================
//  sr_deinit
//============================================================

MODULE_API void sr_deinit()
{
	delete swr;
}


//============================================================
//  sr_init_disp
//============================================================

MODULE_API int sr_init_disp(const char* screen, void* pfdata)
{
	if (screen)
		swr->set_screen(screen);

	display_manager *disp = swr->add_display();
	if (disp == nullptr)
	{
		log_error("%s: error, couldn't add a display\n", __FUNCTION__);
		return -1;
	}

	if (!disp->init(pfdata))
	{
		log_error("%s: error, couldn't init the display\n", __FUNCTION__);
		return -1;
	}

	return disp->index();
}


//============================================================
//  sr_init_disp
//============================================================

MODULE_API void sr_set_disp(int index)
{
	if (index < 0 || index >= (int)swr->displays.size())
		disp_index = 0;
	else
		disp_index = index;
}


//============================================================
//  sr_load_ini
//============================================================

MODULE_API void sr_load_ini(char* config)
{
	swr->parse_config(config);
	sr_disp()->m_ds = swr->ds;
	sr_disp()->parse_options();
}


//============================================================
//  sr_set_monitor
//============================================================

MODULE_API void sr_set_monitor(const char *preset)
{
	swr->set_monitor(preset);
}


//============================================================
//  sr_set_user_mode
//============================================================

MODULE_API void sr_set_user_mode(int width, int height, int refresh)
{
	modeline user_mode = {};
	user_mode.width = width;
	user_mode.height = height;
	user_mode.refresh = refresh;
	swr->set_user_mode(&user_mode);
}


//============================================================
//  sr_get_mode
//============================================================

MODULE_API int sr_get_mode(int width, int height, double refresh, int interlace, sr_mode *srm)
{
	return sr_mode_internal(width, height, refresh, interlace, srm, SR_ACTION_GET, __FUNCTION__);
}


//============================================================
//  sr_add_mode
//============================================================

MODULE_API int sr_add_mode(int width, int height, double refresh, int interlace, sr_mode *srm)
{
	return sr_mode_internal(width, height, refresh, interlace, srm, SR_ACTION_GET | SR_ACTION_FLUSH, __FUNCTION__);
}


//============================================================
//  sr_flush
//============================================================

MODULE_API int sr_flush()
{
	return sr_mode_internal(0, 0, 0, 0, 0, SR_ACTION_FLUSH, __FUNCTION__);
}


//============================================================
//  sr_switch_to_mode
//============================================================

MODULE_API int sr_switch_to_mode(int width, int height, double refresh, int interlace, sr_mode *srm)
{
	return sr_mode_internal(width, height, refresh, interlace, srm, SR_ACTION_GET | SR_ACTION_FLUSH | SR_ACTION_SWITCH, __FUNCTION__);
}


//============================================================
//  sr_set_mode
//============================================================

MODULE_API int sr_set_mode(int id)
{
	sr_mode srm = {};
	srm.id = id;

	return sr_mode_internal(0, 0, 0, 0, &srm, SR_ACTION_GET_FROM_ID | SR_ACTION_SWITCH, __FUNCTION__);
}


//============================================================
//  sr_set_rotation
//============================================================

MODULE_API void sr_set_rotation(int r)
{
	swr->set_rotation(r > 0? true : false);
}


//============================================================
//  sr_set_log_level
//============================================================

MODULE_API void sr_set_log_level(int l)
{
	swr->set_log_level(l);
}


//============================================================
//  sr_set_log_callbacks
//============================================================

MODULE_API void sr_set_log_callback_info(void * f)
{
	swr->set_log_info_fn((void *)f);
}

MODULE_API void sr_set_log_callback_debug(void * f)
{
	swr->set_log_verbose_fn((void *)f);
}

MODULE_API void sr_set_log_callback_error(void * f)
{
	swr->set_log_error_fn((void *)f);
}


//============================================================
//  srlib
//============================================================

MODULE_API srAPI srlib =
{
	sr_init,
	sr_load_ini,
	sr_deinit,
	sr_init_disp,
	sr_set_disp,
	sr_get_mode,
	sr_add_mode,
	sr_switch_to_mode,
	sr_flush,
	sr_set_mode,
	sr_set_monitor,
	sr_set_rotation,
	sr_set_user_mode,
	sr_set_log_level,
	sr_set_log_callback_error,
	sr_set_log_callback_info,
	sr_set_log_callback_debug,
};


//============================================================
// End of Switchres API
//============================================================

//============================================================
//  sr_mode_internal
//============================================================

int sr_mode_internal(int width, int height, double refresh, int interlace, sr_mode *srm, int action, const char *caller)
{
	display_manager *disp = sr_disp();
	if (disp == nullptr)
	{
		log_error("%s: error, didn't get a display\n", caller);
		return 0;
	}

	if (action & SR_ACTION_GET)
	{
		if (srm == nullptr)
		{
			log_error("%s: error, invalid sr_mode pointer\n", caller);
			return 0;
		}

		disp->get_mode(width, height, refresh, (interlace > 0? true : false));
		if (disp->got_mode())
		{
			log_verbose("%s: got mode %dx%d@%f type(%x)\n", caller, disp->width(), disp->height(), disp->v_freq(), disp->selected_mode()->type);
			modeline_to_sr_mode(disp->selected_mode(), srm);
		}
		else
		{
			log_error("%s: error getting mode\n", caller);
			return 0;
		}
	}

	else if (action & SR_ACTION_GET_FROM_ID)
	{
		bool found = false;

		for (auto mode : disp->video_modes)
		{
			if (mode.id == srm->id)
			{
				found = true;
				disp->set_selected_mode(&mode);
				log_verbose("%s: got mode %dx%d@%f type(%x)\n", caller, disp->width(), disp->height(), disp->v_freq(), disp->selected_mode()->type);
				break;
			}
		}

		if (!found)
		{
			log_error("%s: mode ID %d not found\n", caller, srm->id);
			return 0;
		}
	}

	if (action & SR_ACTION_FLUSH)
	{
		if (!disp->flush_modes())
		{
			log_error("%s: error flushing display\n", caller);
			return 0;
		}
	}

	if (action & SR_ACTION_SWITCH)
	{
		if (disp->is_switching_required() || disp->current_mode() != disp->selected_mode())
		{
			if (disp->set_mode(disp->selected_mode()))
				log_info("%s: successfully switched to %dx%d@%f\n", caller, disp->width(), disp->height(), disp->v_freq());
			else
			{
				log_error("%s: error switching to %dx%d@%f\n", caller, disp->width(), disp->height(), disp->v_freq());
				return 0;
			}
		}
		else
			log_info("%s: switching not required\n", caller);
	}

	return 1;
}


//============================================================
//  modeline_to_sr_mode
//============================================================

void modeline_to_sr_mode(modeline* m, sr_mode* srm)
{
	srm->width          = m->hactive;
	srm->height         = m->vactive;
	srm->refresh        = m->vfreq;
	srm->is_refresh_off = sr_disp()->is_refresh_off() ? 1 : 0;
	srm->is_stretched   = sr_disp()->is_stretched() ? 1 : 0;
	srm->x_scale        = sr_disp()->x_scale();
	srm->y_scale        = sr_disp()->y_scale();
	srm->interlace      = sr_disp()->is_interlaced() ? 1 : 0;
	srm->id             = m->id;
}


#ifdef __cplusplus
}
#endif
