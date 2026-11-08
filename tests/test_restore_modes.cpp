// Regression tests for restoring the original mode list by stable ID.
#include "../display.h"
#include <cassert>
#include <cstdio>

static monitor_range range = {};
static generator_settings generator = {};

static modeline make_mode(int id, int width, int height, double refresh)
{
	modeline source = {};
	source.hactive = width;
	source.vactive = height;
	source.vfreq = refresh;

	modeline target = source;
	target.type = XYV_EDITABLE | SCAN_EDITABLE;
	assert(modeline_create(&source, &target, &range, &generator) == 0);
	assert(!(target.result.weight & R_OUT_OF_RANGE));
	assert(target.pclock && target.htotal && target.vtotal);
	target.width = target.hactive;
	target.height = target.vactive;
	target.refresh = int(target.vfreq);
	target.id = id;
	target.type &= ~(MODE_ADD | MODE_UPDATE | MODE_DELETE | MODE_ERROR);
	return target;
}

static modeline *find(display_manager &display, int id)
{
	for (auto &mode : display.video_modes)
		if (mode.id == id) return &mode;
	return nullptr;
}

class failing_video : public custom_video
{
public:
	int fail_id = -1;
	int attempts = 0;
	bool process_modelist(std::vector<modeline *> modes) override
	{
		for (auto *mode : modes)
		{
			if (mode->id == fail_id)
			{
				mode->type |= MODE_ERROR;
				++attempts;
			}
			else mode->type &= ~MODE_ERROR;
		}
		return true;
	}
};

int main()
{
	assert(monitor_set_preset("arcade_15", &range) == 1);
	generator.interlace = 1;
	generator.monitor_aspect = 4.0 / 3.0;
	generator.refresh_tolerance = 2.0;

	display_settings settings = {};
	settings.keep_changes = true;
	dummy_display display(&settings);
	display.backup_modes = {
		make_mode(10, 320, 240, 60.0),
		make_mode(20, 384, 224, 59.0),
		make_mode(30, 640, 240, 60.0)
	};
	display.video_modes = display.backup_modes;

	// Restore changed timings by ID, regardless of the list order.
	display.video_modes = {display.backup_modes[2], make_mode(10, 320, 224, 59.0), display.backup_modes[1]};
	assert(modeline_is_different(find(display, 10), &display.backup_modes[0]));
	assert(display.restore_modes());
	assert(!modeline_is_different(find(display, 10), &display.backup_modes[0]));
	assert(display.video_modes.size() == 3);

	// Delete a mode added during this session, even in the middle of the list.
	display.video_modes.insert(display.video_modes.begin() + 1, make_mode(40, 512, 240, 60.0));
	assert(display.restore_modes());
	assert(find(display, 40) == nullptr);

	// Recover a deleted original mode, without duplicating it on subsequent restores.
	for (auto it = display.video_modes.begin(); it != display.video_modes.end(); ++it)
		if (it->id == 20) { display.video_modes.erase(it); break; }
	assert(display.restore_modes());
	assert(find(display, 20) != nullptr);
	assert(display.video_modes.size() == 3);
	assert(display.restore_modes());
	assert(display.video_modes.size() == 3);

	// A failed addition must retain its pending operation and be retried once.
	failing_video backend;
	backend.fail_id = 20;
	display.set_custom_video(&backend);
	for (auto it = display.video_modes.begin(); it != display.video_modes.end(); ++it)
		if (it->id == 20) { display.video_modes.erase(it); break; }
	assert(!display.restore_modes());
	assert(backend.attempts == 1);
	assert(find(display, 20)->type & (MODE_ADD | MODE_ERROR));
	assert(!display.restore_modes());
	assert(backend.attempts == 2);
	assert(display.video_modes.size() == 3);
	backend.fail_id = -1;
	assert(display.restore_modes());
	assert(!(find(display, 20)->type & (MODE_ADD | MODE_ERROR)));
	assert(display.restore_modes());
	assert(display.video_modes.size() == 3);
	display.set_custom_video(nullptr);
	std::puts("test_restore_modes: PASS");
}
