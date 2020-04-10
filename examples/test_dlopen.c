#include <stdio.h>
#include <stdlib.h>
#ifdef __cplusplus
#include <cstring> // required for strcpy
#endif
#include "cross_dlopen.h"

#ifdef __linux__
#define LIBSWR "libswitchres.so"
#elif _WIN32
#define LIBSWR "libswitchres.dll"
#endif

#include "switchres_wrapper.h"

int main(int argc, char** argv) {
	const char* err_msg;

	printf("About to open %s.\n", LIBSWR);

	// Load the lib
	LIBTYPE dlp = OPENLIB(LIBSWR);

	// Loading failed, inform and exit
	if (!dlp) {
		printf("Loading %s failed.\n", LIBSWR);
		printf("Error: %s\n", LIBERROR());
		exit(EXIT_FAILURE);
	}
	
	printf("Loading %s succeded.\n", LIBSWR);


	// Load the init()
	LIBERROR();
	srAPI* SRobj =  (srAPI*)LIBFUNC(dlp, "srlib");
	if ((err_msg = LIBERROR()) != NULL) {
		printf("Failed to load srAPI: %s\n", err_msg);
		CLOSELIB(dlp);
		exit(EXIT_FAILURE);
	}

	// Testing the function
	printf("Init a new switchres_manager object:\n");
	SRobj->init();

	// Go for a static call
	printf("Testing simple_test()\n");
	SRobj->simple_test();

	// Testing the function
	printf("simple_test_with_params:\n");
	SRobj->simple_test_with_params(320, 240, 60.0, 0, 0);

	// Clean the mess
	printf("Say goodnight:\n");
	SRobj->deinit();

	// We're done, let's closer
	CLOSELIB(dlp);
}
