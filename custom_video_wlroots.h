/**************************************************************

   custom_video_wlroots.h/cpp - Linux Wayland (wlroots)
                                video management layer

   ---------------------------------------------------------

   Switchres   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2021 Chris Kennedy, Antonio Giner,
			 Alexandre Wodarczyk, Gil Delescluse
   Copyright   2026 Mareks Rops

   ---------------------------------------------------------

   This backend talks to wlroots-based compositors (sway,
   Hyprland, etc.) over the wlr-output-management-unstable-v1
   protocol. Unlike the KDE backend, it only supports
   width/height/refresh (no CVT timings) because the wlroots
   protocol doesn't expose full mode timings.

   The wlroots protocol is fundamentally different from KDE's:

   - Custom modes are NOT persistent. set_custom_mode is a
     per-configuration request and only lasts for the duration
     of the active configuration. The compositor does not keep
     a "custom mode list" around between configurations.

   - There is no "replace the whole custom mode list" operation
     analogous to KDE's set_custom_modes. To switch to a custom
     resolution, you build a configuration, call set_custom_mode
     on a head configuration object, and apply.

   - Configurations require a serial. The manager's done event
     carries a serial that must be passed to create_configuration.
     If the configuration becomes stale (e.g. an output was
     hot-plugged between create_configuration and apply), the
     compositor replies with `cancelled` instead of `succeeded`
     or `failed`.

   - Apply responses come in three flavours: succeeded, failed,
     and cancelled. The cancelled case means "your serial was
     stale, re-read the configuration and try again".

   Because custom modes are not persistent, we keep a local
   cache (m_custom_modes) of the modelines switchres has asked
   us to add. get_timing advertises both the compositor's own
   modes and our cached custom modes, and set_timing re-applies
   the custom mode on demand via set_custom_mode.

   The generated protocol sources are vendored under
   protocols/wlroots/ and processed by wayland-scanner at build
   time into:

     protocols/wlroots/wlr-output-management-unstable-v1-client.h
     protocols/wlroots/wlr-output-management-unstable-v1-client-protocol.c

 **************************************************************/

#ifndef __CUSTOM_VIDEO_WLROOTS__
#define __CUSTOM_VIDEO_WLROOTS__

#include <vector>
#include <string>
#include <mutex>
#include <cstdint>

// Wayland core client header (system-installed, found via pkg-config)
#include <wayland-client.h>

#include "custom_video.h"

// Wayland protocol generated header (produced by wayland-scanner at
// build time; see makefile). Plain C, so wrap in extern "C".
extern "C" {
#include "wlr-output-management-unstable-v1-client.h"
}

// Set-timing option flags. Most of xrandr's flags are no-ops under
// Wayland because the compositor performs CRTC relocation atomically
// on apply(). We accept them for API parity and forward only the
// meaningful ones (position/transform via set_position/set_transform).
#define WLROOTS_DISABLE_CRTC_RELOCATION  0x00000001  // accepted, ignored
#define WLROOTS_ENABLE_SCREEN_REORDERING 0x00000002  // emits set_position reqs

// Cached representation of one advertised mode. Populated from the
// zwlr_output_mode_v1 event stream. Unlike the KDE backend, there are
// NO CVT timings here — the wlroots protocol only exposes width/height/
// refresh_mhz on modes. get_timing therefore returns only w/h/refresh
// for advertised modes; for our own custom modes (which switchres
// generated full CVT timings for), we return the full modeline.
struct wlroots_mode_info
{
	zwlr_output_mode_v1 *proxy = nullptr;
	int      id          = 0;       // sequential ID for switchres modeline::id
	bool     current      = false;
	bool     preferred    = false;
	uint32_t width        = 0;
	uint32_t height       = 0;
	int32_t  refresh_mhz  = 0;       // from "refresh" event, milli-Hz
};

// Cached representation of one output head (a zwlr_output_head_v1).
struct wlroots_output
{
	zwlr_output_head_v1 *proxy = nullptr;

	std::string name;            // e.g. "DP-1" or "HDMI-A-1"
	std::string description;     // human-readable (since v1)
	std::string make;             // manufacturer (since v2)
	std::string model;            // model (since v2)
	std::string serial_number;    // serial number (since v2)
	int physical_width_mm  = 0;   // from "physical_size" event
	int physical_height_mm = 0;

	bool       enabled    = false;
	int        x          = 0;
	int        y          = 0;
	int        transform   = 0;
	wl_fixed_t scale       = 0;

	zwlr_output_mode_v1 *current_mode = nullptr;
	std::vector<wlroots_mode_info> modes;

	bool       done_received = false;
};

class wlroots_timing : public custom_video
{
public:
	wlroots_timing(char *device_name, custom_video_settings *vs);
	~wlroots_timing();

	const char *api_name() { return "WLROOTS"; }
	int caps() { return CUSTOM_VIDEO_CAPS_ADD; }
	bool init();

	bool add_mode(modeline *mode);
	bool delete_mode(modeline *mode);
	bool update_mode(modeline *mode);

	bool get_timing(modeline *mode);
	bool set_timing(modeline *mode);

	bool process_modelist(std::vector<modeline *> modelist);

	// ---- Wayland listener thunks (static) ----
	// These must be public because they're referenced from file-scope
	// listener vtables in the .cpp. They forward into the instance
	// via the user-data pointer we attach to every proxy.
	static void registry_global(void *data, wl_registry *r,
	                             uint32_t name, const char *iface, uint32_t version);
	static void registry_global_remove(void *data, wl_registry *r, uint32_t name);

	// zwlr_output_manager_v1 listener (XML event order)
	static void manager_head(void *data, zwlr_output_manager_v1 *mgr,
	                          zwlr_output_head_v1 *head);
	static void manager_done(void *data, zwlr_output_manager_v1 *mgr,
	                         uint32_t serial);
	static void manager_finished(void *data, zwlr_output_manager_v1 *mgr);

	// zwlr_output_head_v1 listener (XML event order, 14 slots)
	static void head_name(void *data, zwlr_output_head_v1 *head,
	                      const char *name);
	static void head_description(void *data, zwlr_output_head_v1 *head,
	                             const char *description);
	static void head_physical_size(void *data, zwlr_output_head_v1 *head,
	                               int32_t width, int32_t height);
	static void head_mode(void *data, zwlr_output_head_v1 *head,
	                      zwlr_output_mode_v1 *mode);
	static void head_enabled(void *data, zwlr_output_head_v1 *head,
	                         int32_t enabled);
	static void head_current_mode(void *data, zwlr_output_head_v1 *head,
	                              zwlr_output_mode_v1 *mode);
	static void head_position(void *data, zwlr_output_head_v1 *head,
	                          int32_t x, int32_t y);
	static void head_transform(void *data, zwlr_output_head_v1 *head,
	                           int32_t transform);
	static void head_scale(void *data, zwlr_output_head_v1 *head,
	                       wl_fixed_t scale);
	static void head_finished(void *data, zwlr_output_head_v1 *head);
	static void head_make(void *data, zwlr_output_head_v1 *head,
	                      const char *make);
	static void head_model(void *data, zwlr_output_head_v1 *head,
	                       const char *model);
	static void head_serial_number(void *data, zwlr_output_head_v1 *head,
	                               const char *serial);
	static void head_adaptive_sync(void *data, zwlr_output_head_v1 *head,
	                                uint32_t state);

	// zwlr_output_mode_v1 listener (XML event order, 4 slots)
	static void mode_size(void *data, zwlr_output_mode_v1 *mode,
	                      int32_t w, int32_t h);
	static void mode_refresh(void *data, zwlr_output_mode_v1 *mode,
	                         int32_t refresh_mhz);
	static void mode_preferred(void *data, zwlr_output_mode_v1 *mode);
	static void mode_finished(void *data, zwlr_output_mode_v1 *mode);

	// zwlr_output_configuration_v1 listener (XML event order, 3 slots)
	static void cfg_succeeded(void *data, zwlr_output_configuration_v1 *cfg);
	static void cfg_failed(void *data, zwlr_output_configuration_v1 *cfg);
	static void cfg_cancelled(void *data, zwlr_output_configuration_v1 *cfg);

private:
	// ---- per-instance identity (mirrors xrandr's m_id/s_id) ----
	int  m_id = 0;
	int  m_managed = 0;
	char m_device_name[32] = {};

	// ---- live Wayland objects ----
	wl_display  *m_display    = nullptr;
	wl_registry *m_registry   = nullptr;

	zwlr_output_manager_v1 *m_manager = nullptr;

	// ---- output cache ----
	wlroots_output *m_desktop_output = nullptr;     // chosen target
	std::vector<wlroots_output *> m_outputs;         // all known heads

	// Snapshot of the output's current_mode at init() time, used by
	// set_timing(MODE_DESKTOP) to restore the original desktop mode.
	// We cache the proxy AND the width/height/refresh. The wlroots
	// protocol doesn't expose CVT timings, so we can only match by
	// w/h/refresh when the proxy is gone.
	zwlr_output_mode_v1 *m_desktop_mode = nullptr;
	uint32_t m_desktop_width  = 0;
	uint32_t m_desktop_height = 0;
	int32_t  m_desktop_refresh_mhz = 0;

	// Serial from the last zwlr_output_manager_v1.done event. Required
	// by create_configuration(serial). The compositor uses it to detect
	// stale configurations; if the configuration changes between
	// create_configuration and apply, the compositor sends `cancelled`
	// (see cfg_cancelled) and the client should re-read the config and
	// retry with a fresh serial.
	uint32_t m_serial = 0;

	// The set of custom modes switchres has asked us to add. The wlroots
	// protocol does NOT persist custom modes between configurations, so
	// we keep this list locally: get_timing advertises them (so switchres
	// sees them in its mode list), and set_timing re-applies them on
	// demand via set_custom_mode. Each entry stores the full modeline
	// switchres generated (with CVT timings) so we can return it from
	// get_timing; only the width/height/refresh are forwarded to the
	// compositor.
	struct custom_mode_entry
	{
		modeline ml;                  // copy of the requested modeline
	};
	std::vector<custom_mode_entry> m_custom_modes;

	// The custom mode currently active on the output (0 = none, meaning
	// an advertised mode is current). Used by delete_mode to decide
	// whether to restore the desktop mode before removing the entry.
	// We track w/h/refresh_mhz because custom modes have no proxy.
	int m_active_custom_w = 0;
	int m_active_custom_h = 0;
	int m_active_custom_refresh_mhz = 0;

	// ---- get_timing cursor (identical pattern to xrandr/KDE) ----
	int m_video_modes_position = 0;

	// ---- async-apply signaling ----
	// These are flipped by the zwlr_output_configuration_v1 listener
	// callbacks (which run during wl_display_roundtrip on the calling
	// thread in the single-threaded design).
	bool        m_apply_done         = false;
	bool        m_apply_ok           = false;
	bool        m_apply_cancelled    = false;  // serial was stale
	std::string m_apply_failure_reason;

	// ---- synchronization ----
    // m_mutex protects the output/mode cache (m_outputs, m_custom_modes,
    // m_desktop_mode, etc.) from re-entrancy during wl_display_roundtrip.
    // A roundtrip can dispatch multiple events, and listener callbacks
    // take this lock to update the cache. In the single-threaded design
    // the lock is uncontended, but it keeps the code thread-safe if a
    // dispatch thread is ever added.
    std::mutex              m_mutex;

	// ---- helpers ----
	void   release_all_outputs();

	bool   pump_until_apply_done();

	bool   find_output_by_name(const char *name, wlroots_output *&out);
	wlroots_mode_info *find_mode_by_proxy(zwlr_output_mode_v1 *proxy);
	custom_mode_entry *find_custom_mode_for(const modeline *m);

	void   modeline_from_mode_info(const wlroots_mode_info *mi, modeline *out);
	void   modeline_from_custom_entry(const custom_mode_entry *e, modeline *out);

	// Build a configuration for the desktop output that either switches
	// to an advertised mode (target_proxy != nullptr, uses set_mode) or
	// applies a custom mode (target_proxy == nullptr, uses set_custom_mode
	// with custom_w/custom_h/custom_refresh_mhz). Handles serial-stale
	// retries internally. Returns true on succeeded, false on failed or
	// after exhausting retries.
	bool   apply_configuration_for_mode(zwlr_output_mode_v1 *target_proxy,
	                                    int custom_w, int custom_h, int custom_refresh_mhz,
	                                    int x, int y, int transform);

	// ---- private static helpers (need access to m_outputs etc.) ----
	static wlroots_output *find_output_by_proxy_locked(
	    wlroots_timing *self, zwlr_output_head_v1 *proxy);
	static wlroots_mode_info *find_mode_info_locked(
	    wlroots_timing *self, zwlr_output_mode_v1 *proxy);
};

#endif
