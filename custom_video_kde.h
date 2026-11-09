/**************************************************************

   custom_video_kde.h - Linux Wayland (KDE Output Management v2)
			    video management layer

   ---------------------------------------------------------

   Switchres   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2021 Chris Kennedy, Antonio Giner,
			 Alexandre Wodarczyk, Gil Delescluse
   Copyright   2026 Mareks Rops

   ---------------------------------------------------------

   This backend talks to KDE Plasma (and any compositor implementing
   the kde_output_device_v2 / kde_output_management_v2 protocols)
   over the Wayland wire protocol.

   Unlike the xrandr backend (which dlopens libXrandr.so because it
   builds the X11 calls by hand), this backend uses the standard
   wayland-scanner-generated client code, which references
   wl_proxy_marshal_constructor and friends as ordinary externals.
   We therefore link directly with libwayland-client at build time.
   The makefile gates the build on `pkg-config wayland-client`, so
   systems without Wayland development files simply get the backend
   disabled, exactly like the existing KMSDRM path.

   The KDE protocol XMLs are vendored under protocols/kde/ and are
   processed by wayland-scanner at build time into:

     protocols/kde/kde-output-device-v2-client.h
     protocols/kde/kde-output-device-v2-client-protocol.c
     protocols/kde/kde-output-management-v2-client.h
     protocols/kde/kde-output-management-v2-client-protocol.c

 **************************************************************/

#ifndef __CUSTOM_VIDEO_KDE__
#define __CUSTOM_VIDEO_KDE__

#include <vector>
#include <string>
#include <mutex>
#include <cstdint>

// Wayland core client header (system-installed, found via pkg-config)
#include <wayland-client.h>

#include "custom_video.h"

// Wayland protocol generated headers (produced by wayland-scanner at
// build time; see makefile). These are plain C, so wrap in extern "C".
extern "C" {
#include "kde-output-device-v2-client.h"
#include "kde-output-management-v2-client.h"
}

// Set-timing option flags. Most of xrandr's flags are no-ops under Wayland
// because the compositor performs CRTC relocation atomically on apply().
// We accept them for API parity and forward only the meaningful ones.
#define KDE_DISABLE_CRTC_RELOCATION   0x00000001  // accepted, ignored
#define KDE_ENABLE_SCREEN_REORDERING  0x00000002  // emits position() reqs

// capability bit (kde_output_device_v2.capabilities enum)
#define KDE_CAP_CUSTOM_MODES          0x00002000  // since v18
#define KDE_CAP_DISABLE               0x00020000  // since v25

// DRM_MODE_FLAG_* values used by kde_mode_list_v2.add_cvt. We define them
// here so the build does not require libdrm headers; the values are
// identical to those in <drm_mode.h>.
#define KDE_DRM_MODE_FLAG_PHSYNC      0x00000001
#define KDE_DRM_MODE_FLAG_NHSYNC      0x00000002
#define KDE_DRM_MODE_FLAG_PVSYNC      0x00000004
#define KDE_DRM_MODE_FLAG_NVSYNC      0x00000008
#define KDE_DRM_MODE_FLAG_INTERLACE   0x00000010
#define KDE_DRM_MODE_FLAG_DBLSCAN     0x00000020

// Cached representation of one advertised mode. Populated from the
// kde_output_device_mode_v2 event stream. For modes we created ourselves
// we additionally remember the originating modeline so get_timing can
// return full timings even on compositors that don't emit the cvt event.
struct kde_mode_info
{
	kde_output_device_mode_v2 *proxy = nullptr;
	int      id          = 0;       // sequential ID for switchres modeline::id
	bool     current      = false;
	bool     preferred    = false;
	bool     is_ours      = false;       // true for modes we added via set_custom_modes
	uint32_t width        = 0;
	uint32_t height       = 0;
	int32_t  refresh_mhz  = 0;           // from "refresh" event, milli-Hz
	uint32_t flags        = 0;           // from "flags" event

	// Full CVT timings, only present if the compositor emitted the cvt
	// event (protocol v24+) OR if we created the mode ourselves.
	bool     has_cvt      = false;
	uint32_t cvt_dot_clock_khz = 0;
	uint32_t cvt_hdisplay = 0, cvt_hsync_start = 0, cvt_hsync_end = 0;
	uint32_t cvt_htotal   = 0, cvt_hskew      = 0;
	uint32_t cvt_vdisplay = 0, cvt_vsync_start = 0, cvt_vsync_end = 0;
	uint32_t cvt_vtotal   = 0, cvt_vscan      = 0;
	uint32_t cvt_flags    = 0;
};

// Cached representation of one output device.
struct kde_output
{
	kde_output_device_v2 *proxy = nullptr;

	std::string name;            // e.g. "DP-1" (since v2)
	std::string uuid;            // stable id
	std::string make;
	std::string model;
	std::string serial_number;
	std::string eisa_id;

	bool       enabled    = false;
	int        x          = 0;
	int        y          = 0;
	int        transform   = 0;
	uint32_t   capabilities = 0;

	kde_output_device_mode_v2 *current_mode = nullptr;
	std::vector<kde_mode_info> modes;

	bool       done_received = false;
	// (Output proxies come from kde_output_device_registry_v2.output, not
	// from wl_registry_bind, so they have no wl_registry global id. Output
	// removal is signalled by kde_output_device_v2.removed, not by
	// wl_registry.global_remove.)
};

class kde_timing : public custom_video
{
public:
	kde_timing(char *device_name, custom_video_settings *vs);
	~kde_timing();

	const char *api_name() { return "KDE"; }
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

	// kde_output_device_registry_v2 listener
	static void device_registry_finished(void *data,
					      kde_output_device_registry_v2 *reg);
	static void device_registry_output(void *data,
					    kde_output_device_registry_v2 *reg,
					    kde_output_device_v2 *output);

	// kde_output_device_v2 listener (XML event order)
	static void device_geometry(void *data, kde_output_device_v2 *dev,
				    int32_t x, int32_t y,
				    int32_t phys_w, int32_t phys_h,
				    int32_t subpixel, const char *make,
				    const char *model, int32_t transform);
	static void device_current_mode(void *data, kde_output_device_v2 *dev,
					kde_output_device_mode_v2 *mode);
	static void device_mode(void *data, kde_output_device_v2 *dev,
				kde_output_device_mode_v2 *mode);
	static void device_done(void *data, kde_output_device_v2 *dev);
	static void device_scale(void *data, kde_output_device_v2 *dev,
				 wl_fixed_t scale);
	static void device_edid(void *data, kde_output_device_v2 *dev,
				const char *raw);
	static void device_enabled(void *data, kde_output_device_v2 *dev,
				   int32_t enabled);
	static void device_uuid(void *data, kde_output_device_v2 *dev,
				const char *uuid);
	static void device_serial_number(void *data, kde_output_device_v2 *dev,
					 const char *serial);
	static void device_eisa_id(void *data, kde_output_device_v2 *dev,
				   const char *eisa);
	static void device_capabilities(void *data, kde_output_device_v2 *dev,
					uint32_t flags);
	static void device_name(void *data, kde_output_device_v2 *dev,
				const char *name);
	static void device_removed(void *data, kde_output_device_v2 *dev);

	// ---- unused event stubs ----
	// These are no-op stubs for events the backend doesn't handle but
	// must still provide in the listener vtable (libwayland aborts if
	// a slot is NULL). Each has the exact signature the protocol
	// expects for its slot.
	static void device_overscan(void *data, kde_output_device_v2 *dev, uint32_t overscan);
	static void device_vrr_policy(void *data, kde_output_device_v2 *dev, uint32_t vrr_policy);
	static void device_rgb_range(void *data, kde_output_device_v2 *dev, uint32_t rgb_range);
	static void device_high_dynamic_range(void *data, kde_output_device_v2 *dev, uint32_t hdr_enabled);
	static void device_sdr_brightness(void *data, kde_output_device_v2 *dev, uint32_t sdr_brightness);
	static void device_wide_color_gamut(void *data, kde_output_device_v2 *dev, uint32_t wcg_enabled);
	static void device_auto_rotate_policy(void *data, kde_output_device_v2 *dev, uint32_t policy);
	static void device_icc_profile_path(void *data, kde_output_device_v2 *dev, const char *profile_path);
	static void device_brightness_metadata(void *data, kde_output_device_v2 *dev, uint32_t max_peak_brightness, uint32_t max_frame_average_brightness, uint32_t min_brightness);
	static void device_brightness_overrides(void *data, kde_output_device_v2 *dev, int32_t max_peak_brightness, int32_t max_average_brightness, int32_t min_brightness);
	static void device_sdr_gamut_wideness(void *data, kde_output_device_v2 *dev, uint32_t gamut_wideness);
	static void device_color_profile_source(void *data, kde_output_device_v2 *dev, uint32_t source);
	static void device_brightness(void *data, kde_output_device_v2 *dev, uint32_t brightness);
	static void device_color_power_tradeoff(void *data, kde_output_device_v2 *dev, uint32_t preference);
	static void device_dimming(void *data, kde_output_device_v2 *dev, uint32_t multiplier);
	static void device_replication_source(void *data, kde_output_device_v2 *dev, const char *source);
	static void device_ddc_ci_allowed(void *data, kde_output_device_v2 *dev, uint32_t allowed);
	static void device_max_bits_per_color(void *data, kde_output_device_v2 *dev, uint32_t max_bpc);
	static void device_max_bits_per_color_range(void *data, kde_output_device_v2 *dev, uint32_t min_value, uint32_t max_value);
	static void device_automatic_max_bits_per_color_limit(void *data, kde_output_device_v2 *dev, uint32_t max_bpc_limit);
	static void device_edr_policy(void *data, kde_output_device_v2 *dev, uint32_t policy);
	static void device_sharpness(void *data, kde_output_device_v2 *dev, uint32_t sharpness);
	static void device_priority(void *data, kde_output_device_v2 *dev, uint32_t priority);
	static void device_auto_brightness(void *data, kde_output_device_v2 *dev, uint32_t enabled);
	static void device_hdr_icc_profile_path(void *data, kde_output_device_v2 *dev, const char *profile_path);
	static void device_hdr_color_profile_source(void *data, kde_output_device_v2 *dev, uint32_t source);
	static void device_abm_level(void *data, kde_output_device_v2 *dev, uint32_t level);

	// kde_output_device_mode_v2 listener (XML event order)
	static void mode_size(void *data, kde_output_device_mode_v2 *mode,
			     int32_t w, int32_t h);
	static void mode_refresh(void *data, kde_output_device_mode_v2 *mode,
				 int32_t refresh_mhz);
	static void mode_preferred(void *data, kde_output_device_mode_v2 *mode);
	static void mode_removed(void *data, kde_output_device_mode_v2 *mode);
	static void mode_flags(void *data, kde_output_device_mode_v2 *mode,
			       uint32_t flags);
	static void mode_cvt(void *data, kde_output_device_mode_v2 *mode,
			     uint32_t dot_clock_khz,
			     uint32_t hdisplay, uint32_t hsync_start, uint32_t hsync_end,
			     uint32_t htotal, uint32_t hskew,
			     uint32_t vdisplay, uint32_t vsync_start, uint32_t vsync_end,
			     uint32_t vtotal, uint32_t vscan, uint32_t flags);

	// kde_output_configuration_v2 listener (XML event order)
	static void cfg_applied(void *data, kde_output_configuration_v2 *cfg);
	static void cfg_failed(void *data, kde_output_configuration_v2 *cfg);
	static void cfg_failure_reason(void *data, kde_output_configuration_v2 *cfg,
				       const char *reason);

private:
	// ---- per-instance identity (mirrors xrandr's m_id/s_id) ----
	int  m_id = 0;
	int  m_managed = 0;
	char m_device_name[32] = {};

	// ---- live Wayland objects ----
	wl_display  *m_display    = nullptr;
	wl_registry *m_registry   = nullptr;

	kde_output_device_registry_v2 *m_device_registry = nullptr;
	kde_output_management_v2       *m_management     = nullptr;

	// ---- output cache ----
	kde_output *m_desktop_output = nullptr;     // chosen target
	std::vector<kde_output *> m_outputs;         // all known outputs

	// Snapshot of the output's current_mode at init() time, used by
	// set_timing(MODE_DESKTOP) to restore the original desktop mode even
	// after we've switched to custom modes. Mirrors xrandr's m_desktop_mode.
	kde_output_device_mode_v2 *m_desktop_mode = nullptr;

	// Cached timing of the desktop mode (snapshotted at init). If the
	// compositor destroys the desktop mode proxy (e.g. during a
	// set_custom_modes that removes it), m_desktop_mode becomes null,
	// and we use these cached values to find the mode again in the
	// refreshed mode list. We cache the full CVT timing (not just
	// width/height/refresh) because a monitor can advertise multiple
	// modes with the same resolution but different timings (e.g. CVT
	// vs CVT-RB with different pixel clocks).
	uint32_t m_desktop_width  = 0;
	uint32_t m_desktop_height = 0;
	int32_t  m_desktop_refresh_mhz = 0;
	uint32_t m_desktop_dot_clock_khz = 0;
	uint32_t m_desktop_hsync_start = 0, m_desktop_hsync_end = 0, m_desktop_htotal = 0;
	uint32_t m_desktop_vsync_start = 0, m_desktop_vsync_end = 0, m_desktop_vtotal = 0;
	uint32_t m_desktop_cvt_flags = 0;

	// The set of custom modes we have registered with the compositor.
    // Because set_custom_modes is a *replacement* operation (not
    // incremental), we keep this list and re-send it in full on every
    // add/delete.
    //
    // is_preserved marks entries captured at init() time from the
    // compositor's advertised list (every mode with flags & 0x1).
    // delete_mode() and add_mode()'s duplicate-check both skip
    // is_preserved entries — the user can neither delete nor "re-add"
    // a mode the compositor was already advertising before we connected.
    // This protects:
    //   - the desktop mode (when it's itself a custom mode), so the
    //     protocol's set_custom_modes replace semantics don't wipe it
    //     out from under us
    //   - any other custom modes the user set up via other tools
    //     (KDE display settings, a previous switchres run with
    //     keep_changes=true, another Wayland client) — without this,
    //     our first add_mode would wipe out the user's pre-existing
    //     custom mode configuration
	struct custom_mode_entry
	{
		modeline ml;                  // copy of the requested modeline
		kde_output_device_mode_v2 *proxy = nullptr;  // matched proxy after apply
		bool is_preserved = false;    // true = captured at init, never delete
	};
	std::vector<custom_mode_entry> m_custom_modes;

	// ---- get_timing cursor (identical pattern to xrandr) ----
	int m_video_modes_position = 0;

	// ---- async-apply signaling ----
	// These are flipped by the kde_output_configuration_v2 listener
	// callbacks (which run on the dispatch thread).
	bool        m_apply_done         = false;
	bool        m_apply_ok           = false;
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

	void   pump_events();
	bool   pump_until_apply_done();

	bool   find_output_by_name(const char *name, kde_output *&out);
	kde_mode_info *find_advertised_mode_for(const modeline *m);
	kde_mode_info *find_mode_by_proxy(kde_output_device_mode_v2 *proxy);

	uint32_t drm_flags_from_modeline(const modeline *m);
	void     modeline_from_mode_info(const kde_mode_info *mi, modeline *out);

	bool   rebuild_custom_modes_and_apply();
	bool   send_apply_for_mode_switch(kde_output_device_mode_v2 *target_proxy,
					  int x, int y, int transform);

	// ---- private static helpers (need access to m_outputs etc.) ----
	static kde_output *find_output_by_proxy_locked(
	    kde_timing *self, kde_output_device_v2 *proxy);
	static kde_mode_info *find_mode_info_locked(
	    kde_timing *self, kde_output_device_mode_v2 *proxy);
};

#endif
