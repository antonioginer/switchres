#include "switchres_wrapper.h"

int main(int argc, char** argv) {
	sr_init();
	simple_test();
	simple_test_with_params(384, 224, 59.63, 0, 0);
	sr_deinit();
}
