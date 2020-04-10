#define MODULE_API_EXPORTS
#include "switchres.h"
#include "switchres_wrapper.h"
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#ifdef MODULE_API_EXPORTS
#define MODULE_API __declspec(dllexport)
#else
#define MODULE_API __declspec(dllimport)
#endif
#else
#define MODULE_API
#endif

switchres_manager* swr;


MODULE_API void sr_init() {
	swr = new switchres_manager;
	swr->set_log_verbose_fn((void *)printf);
	swr->set_log_info_fn((void *)printf);
	swr->set_log_error_fn((void *)printf);
	swr->parse_config("switchres.ini");
}

MODULE_API void sr_deinit() {
	delete swr;
}

MODULE_API void sr_set_monitor(const char *preset) {
	swr->set_monitor(preset);
	swr->add_display();
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

MODULE_API srAPI srlib = { sr_init, sr_deinit, simple_test, simple_test_with_params};

#ifdef __cplusplus
}
#endif
