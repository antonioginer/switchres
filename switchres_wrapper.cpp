#define MODULE_API_EXPORTS
#include "switchres.h"
#include "switchres_wrapper.h"
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

switchres_manager* swr;


MODULE_API void sr_init() {
	swr = new switchres_manager;
	//swr->set_log_verbose_fn((void *)printf);
	swr->set_log_info_fn((void *)printf);
	swr->set_log_error_fn((void *)printf);
	swr->parse_config("switchres.ini");
	swr->add_display();
	for (auto &display : swr->displays)
		display->init();
}


MODULE_API void sr_deinit() {
	delete swr;
}


MODULE_API void sr_set_monitor(const char *preset) {
	swr->set_monitor(preset);
}

unsigned char disp_best_mode_to_sr_mode(display_manager* disp, sr_mode* srm)
{
	if (!disp->got_mode()) return 0;

	srm->width = disp->width();
	srm->height = disp->height();
	srm->refresh = disp->refresh();
	srm->is_refresh_off = (disp->is_refresh_off() ? 1 : 0);
	srm->is_stretched = (disp->is_stretched() ? 1 : 0);
	srm->x_scale = disp->x_scale();
	srm->y_scale = disp->y_scale();
	srm->interlace = (disp->is_interlaced() ? 105 : 0);

	return 1;
}

MODULE_API unsigned char sr_get_mode(int width, int height, double refresh, unsigned char interlace, sr_mode *return_mode) {
	bool linterlace = (interlace > 0) ? true : false;

	display_manager *disp = swr->display();
	printf("Modeline parameters: %dx%d@%f\n", width, height, refresh);
	modeline *mode = disp->get_mode(width, height, refresh, linterlace);
	return disp_best_mode_to_sr_mode(disp, return_mode);
}


MODULE_API unsigned char sr_add_mode(int width, int height, double refresh, unsigned char interlace, sr_mode *return_mode) {
	bool linterlace = (interlace > 0) ? true : false;

	printf("Inside sr_add_mode\n");
	display_manager *disp = swr->display();
	printf("Got a display\n");
	printf("Modeline parameters: %dx%d@%f\n", width, height, refresh);
	modeline *mode = disp->get_mode(width, height, refresh, linterlace);
	printf("Got a mode\n");
	if (!disp_best_mode_to_sr_mode(disp, return_mode)) return 0;
	for (auto &display : swr->displays)
	{
		mode = display->get_mode(width, height, refresh, linterlace);
		if (!mode) continue;
		if (mode->type & MODE_UPDATED) {
			printf("sr_get_mode: required mode updated\n");
			disp->update_mode(mode);
		}
		else if (mode->type & MODE_NEW)
		{
			printf("sr_get_mode: required mode was added\n");
			disp->add_mode(mode);
		}
	}
	return 1;
}


MODULE_API unsigned char sr_switch_to_mode(int width, int height, double refresh, unsigned char interlace, sr_mode *return_mode) {
	bool linterlace = (interlace > 0) ? true : false;

	printf("Inside sr_switch_to_mode\n");
	display_manager *disp = swr->display();
	modeline *mode = disp->get_mode(width, height, refresh, linterlace);
	if (!disp_best_mode_to_sr_mode(disp, return_mode)) return 0;
	printf("mode type: %d\n", mode->type);
	if (mode->type & MODE_UPDATED) {
		printf("sr_switch_to_mode: Required mode updated\n");
		disp->update_mode(mode);
	}
	else if (mode->type & MODE_NEW) 
	{
		printf("sr_switch_to_mode: Warning: required mode had to be added\n");
		disp->add_mode(mode);
	}
	if (disp->set_mode(mode))
		return 1;
	else
		return 0;
}


MODULE_API void simple_test() {
	printf("Inside simple_test\n");
	swr->set_log_verbose_fn((void*)printf);
	swr->set_monitor("generic_15");
	swr->add_display();
	modeline *mode = swr->display()->get_mode(384, 288, 50.0, false);
}


MODULE_API void simple_test_with_params(int width, int height, double refresh, unsigned char interlace, unsigned char rotate) {
	bool linterlace = false, lrotate = false;

	if (interlace > 0)
		linterlace = true;

	if (rotate > 0)
		lrotate = true;

	printf("Inside simple_test_with_params\n");
	swr->set_log_verbose_fn((void*)printf);
	swr->set_monitor("generic_15");
	swr->add_display();
	modeline *mode = swr->display()->get_mode(width, height, refresh, linterlace);
}


MODULE_API srAPI srlib = { 
	sr_init, 
	sr_deinit, 
	sr_get_mode, 
	sr_add_mode, 
	sr_switch_to_mode
};

#ifdef __cplusplus
}
#endif
