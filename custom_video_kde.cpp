/**************************************************************

   custom_video_kde.cpp - Linux Wayland (KDE Output Management v2)
				video management layer

   ---------------------------------------------------------

   Switchres   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2021 Chris Kennedy, Antonio Giner,
			 Alexandre Wodarczyk, Gil Delescluse
   Copyright   2026 Mareks Rops

   ---------------------------------------------------------

   See custom_video_kde.h for design notes.

   Threading model
   ---------------
   Wayland is asynchronous; switchres' custom_video API is synchronous.
   We bridge the two with a single-threaded design: every public API
   method sends wire requests, then calls wl_display_roundtrip() on the
   caller's thread to block until the compositor responds. All listener
   callbacks run on the calling thread during the roundtrip and serialise
   on m_mutex (uncontended in practice, but kept for thread-safety if a
   dispatch thread is ever added).

 **************************************************************/

#include <stdio.h>
#include <exception>
#include <string.h>
#include <chrono>

#include "custom_video_kde.h"
#include "log.h"

// =========================================================================
//  Compile the wayland-scanner-generated protocol source files directly
//  into this translation unit. The generated .c files define the interface
//  metadata structs (e.g. kde_output_device_v2_interface) and the request
//  marshalling code, all of which are referenced by the inline wrappers in
//  the generated headers. Including them here (wrapped in extern "C")
//  keeps the makefile simple - we don't need a separate .c -> .o build
//  rule, and there are no extra objects to thread through the link line.
//  The generated headers have include guards so the double inclusion
//  (via custom_video_kde.h above and via each .c below) is harmless.
//
//  The generated code marks interface structs with WL_PRIVATE
//  (__attribute__((visibility("hidden"))), which triggers
//  -Wattributes warnings when compiled as C++ without -fvisibility=hidden.
//  Suppress those warnings locally — they're cosmetic; the structs work
//  fine with default visibility in a static build.
// =========================================================================
#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wattributes"
#endif
extern "C" {
#include "kde-output-device-v2-client-protocol.c"
#include "kde-output-management-v2-client-protocol.c"
}
#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

// =========================================================================
//  Static state (mirrors xrandr's s_id / s_total_managed_screen)
// =========================================================================

static int s_id = 0;
static int s_total_managed_screen = 0;

// =========================================================================
//  Listener vtables. The struct field order matches the XML event
//  declaration order; slots we don't use are set to nullptr.
// =========================================================================

static const wl_registry_listener registry_listener = {
	kde_timing::registry_global,
	kde_timing::registry_global_remove
};

// The wl_registry listener is what binds the KDE globals.
// The kde_output_device_registry_v2 listener (with `device_registry_output` as
// the `output` event thunk) is defined later in this file, just below the
// thunk it references.

// kde_output_device_v2: 40 events in XML declaration order.
// We only handle the ones needed for enumeration + mode switching; the
// rest are wired to no-op stubs (libwayland aborts if a listener slot
// is NULL, even for events we don't care about).
static const kde_output_device_v2_listener device_listener = {
	kde_timing::device_geometry,
	kde_timing::device_current_mode,
	kde_timing::device_mode,
	kde_timing::device_done,
	kde_timing::device_scale,
	kde_timing::device_edid,
	kde_timing::device_enabled,
	kde_timing::device_uuid,
	kde_timing::device_serial_number,
	kde_timing::device_eisa_id,
	kde_timing::device_capabilities,
	kde_timing::device_overscan,
	kde_timing::device_vrr_policy,
	kde_timing::device_rgb_range,
	kde_timing::device_name,                // since v2
	kde_timing::device_high_dynamic_range,
	kde_timing::device_sdr_brightness,
	kde_timing::device_wide_color_gamut,
	kde_timing::device_auto_rotate_policy,
	kde_timing::device_icc_profile_path,
	kde_timing::device_brightness_metadata,
	kde_timing::device_brightness_overrides,
	kde_timing::device_sdr_gamut_wideness,
	kde_timing::device_color_profile_source,
	kde_timing::device_brightness,
	kde_timing::device_color_power_tradeoff,
	kde_timing::device_dimming,
	kde_timing::device_replication_source,
	kde_timing::device_ddc_ci_allowed,
	kde_timing::device_max_bits_per_color,
	kde_timing::device_max_bits_per_color_range,
	kde_timing::device_automatic_max_bits_per_color_limit,
	kde_timing::device_edr_policy,
	kde_timing::device_sharpness,
	kde_timing::device_priority,
	kde_timing::device_auto_brightness,
	kde_timing::device_removed,             // since v21
	kde_timing::device_hdr_icc_profile_path,
	kde_timing::device_hdr_color_profile_source,
	kde_timing::device_abm_level
};

static const kde_output_device_mode_v2_listener mode_listener = {
	kde_timing::mode_size,
	kde_timing::mode_refresh,
	kde_timing::mode_preferred,
	kde_timing::mode_removed,
	kde_timing::mode_flags,       // since v19
	kde_timing::mode_cvt          // since v24
};

static const kde_output_configuration_v2_listener configuration_listener = {
	kde_timing::cfg_applied,
	kde_timing::cfg_failed,
	kde_timing::cfg_failure_reason  // since v12
};

// =========================================================================
//  kde_output_device_registry_v2 listener thunk: `output` event.
//  The compositor sends one `output` event per available output, with a
//  freshly-created kde_output_device_v2 proxy. We allocate a kde_output
//  cache entry and attach our device_listener so subsequent geometry/mode/etc
//  events populate it. Must be declared here so the listener struct below
//  is visible to the constructor.
// =========================================================================

void kde_timing::device_registry_output(void *data,
					   kde_output_device_registry_v2 * /*reg*/,
					   kde_output_device_v2 *output)
{
	auto *self = static_cast<kde_timing *>(data);
	if (!output) return;
	std::lock_guard<std::mutex> lock(self->m_mutex);

	auto *out = new kde_output();
	out->proxy = output;
	// No registry_name: output proxies come from the device registry's
	// `output` event, not from wl_registry_bind, so they have no
	// wl_registry global id.

	kde_output_device_v2_add_listener(output, &device_listener, self);
	self->m_outputs.push_back(out);
	log_verbose("KDE: <%d> (device_registry_output) new output proxy %p\n",
		    self->m_id, (void *)output);
}

static const kde_output_device_registry_v2_listener device_registry_listener = {
	kde_timing::device_registry_finished,  // finished (destructor event, since v21)
	kde_timing::device_registry_output      // output
};

// =========================================================================
//  Constructor
// =========================================================================

kde_timing::kde_timing(char *device_name, custom_video_settings *vs)
{
	m_vs = *vs;
	m_id = ++s_id;

	log_verbose("KDE: <%d> (kde_timing) creation (%s)\n", m_id, device_name);

	// Copy device name with the same truncation behaviour as xrandr.
	if ((strlen(device_name) + 1) > 32)
	{
		strncpy(m_device_name, device_name, 31);
		log_error("KDE: <%d> (kde_timing) [ERROR] device name truncated to %s\n", m_id, m_device_name);
	}
	else
		strcpy(m_device_name, device_name);

	// Connect to the Wayland display. wl_display_connect returns NULL on
	// failure (no compositor, WAYLAND_DISPLAY unset, etc.).
	m_display = wl_display_connect(NULL);
	if (!m_display)
	{
		log_verbose("KDE: <%d> (kde_timing) Wayland server not found\n", m_id);
		throw std::exception();
	}

	// Acquire the registry; we'll bind globals from it during init().
	m_registry = wl_display_get_registry(m_display);
	if (!m_registry)
	{
		log_error("KDE: <%d> (kde_timing) [ERROR] wl_display_get_registry failed\n", m_id);
		wl_display_disconnect(m_display);
		m_display = nullptr;
		throw std::exception();
	}

	wl_registry_add_listener(m_registry, &registry_listener, this);

	// One roundtrip to bind kde_output_device_registry_v2 and
	// kde_output_management_v2 (registry_global() runs here).
	wl_display_roundtrip(m_display);

	if (!m_device_registry || !m_management)
	{
		log_error("KDE: <%d> (kde_timing) [ERROR] compositor does not advertise KDE output protocols\n", m_id);
		throw std::exception();
	}

	// Now bind the device registry - it will start emitting `output`
	// events, each creating a kde_output_device_v2 proxy. The device
	// proxy then emits geometry/mode/current_mode/done events.
	kde_output_device_registry_v2_add_listener(m_device_registry,
						    &device_registry_listener, this);

	// Two roundtrips to drain the initial burst of output/mode events so
	// that init() sees a fully-populated cache. The compositor processes
	// events in order: bind -> output* -> mode* (per output) -> done (per
	// output) -> sync-done. A single roundtrip usually suffices; the
	// second one handles the case where mode sub-events arrive after the
	// device's done event.
	wl_display_roundtrip(m_display);
	wl_display_roundtrip(m_display);

	s_total_managed_screen++;
}

// =========================================================================
//  Destructor
// =========================================================================

kde_timing::~kde_timing()
{
	s_total_managed_screen--;
	if (s_total_managed_screen == 0)
		s_id = 0;

	// Single-threaded design: no dispatch thread to stop. Just release
	// Wayland resources in reverse order of acquisition.
	release_all_outputs();

	if (m_management)
		kde_output_management_v2_destroy(m_management);
	if (m_device_registry)
		kde_output_device_registry_v2_destroy(m_device_registry);
	if (m_registry)
		wl_registry_destroy(m_registry);
	if (m_display)
		wl_display_disconnect(m_display);
}

// =========================================================================
//  release_all_outputs
// =========================================================================

void kde_timing::release_all_outputs()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto *out : m_outputs)
    {
        if (!out) continue;
        // Destroy mode proxies first — the compositor may not send
        // mode.removed for each mode when the output is released, so
        // we destroy them explicitly here to avoid leaking client-side
        // wl_proxy objects.
        for (auto &mi : out->modes)
        {
                if (mi.proxy)
                        kde_output_device_mode_v2_destroy(mi.proxy);
        }
        out->modes.clear();
        // Now release the output proxy.
        if (out->proxy)
        {
            uint32_t v = kde_output_device_v2_get_version(out->proxy);
            if (v >= 21)
                    kde_output_device_v2_release(out->proxy);
            else
                    kde_output_device_v2_destroy(out->proxy);
        }
        delete out;
    }
    m_outputs.clear();
    m_desktop_output = nullptr;
}

// =========================================================================
//  init()
// =========================================================================

bool kde_timing::init()
{
    log_verbose("KDE: <%d> (init) waiting for compositor state to settle\n", m_id);

    // Make sure initial output/mode events have been processed.
    wl_display_roundtrip(m_display);
    wl_display_roundtrip(m_display);

    bool detected = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        log_verbose("KDE: <%d> (init) %d output(s) discovered\n",
                    m_id, (int)m_outputs.size());

        // Diagnostic: print mode count for each output BEFORE selection
        for (auto *out : m_outputs)
        {
            if (!out || !out->proxy) continue;
            log_verbose("KDE: <%d> (init) output '%s' has %zu mode(s), enabled=%d, current_mode=%p\n",
                m_id, out->name.c_str(), out->modes.size(),
                (int)out->enabled, (void *)out->current_mode);
            for (size_t i = 0; i < out->modes.size(); i++)
            {
                const auto &mi = out->modes[i];
                log_verbose("KDE: <%d> (init)   [%zu] %p %ux%u@%.3f cvt=%d\n",
                    m_id, i, (void *)mi.proxy, mi.width, mi.height,
                    mi.refresh_mhz / 1000.0, mi.has_cvt ? 1 : 0);
            }
        }

        kde_output *chosen = nullptr;

        // Parse the device name. Three forms are accepted, matching
        // the xrandr backend's conventions:
        //   - "auto"          : pick the first enabled output
        //   - connector name  : e.g. "eDP-1", "DP-1", "HDMI-A-1"
        //   - "screen0".."screen9" or "0".."9" : the Nth output by
        //                       index (the switchres manager's
        //                       display index as a string)
        // The screen-position form is what groovy mame (and the
        // switchres C wrapper's default) passes — without it, the
        // KDE backend would fail with "no screen detected" whenever
        // the caller doesn't explicitly use "auto" or a connector
        // name.
        int screen_pos = -1;
        if (strlen(m_device_name) == 7 && !strncmp(m_device_name, "screen", 6) &&
            m_device_name[6] >= '0' && m_device_name[6] <= '9')
            screen_pos = m_device_name[6] - '0';
        else if (strlen(m_device_name) == 1 && m_device_name[0] >= '0' && m_device_name[0] <= '9')
            screen_pos = m_device_name[0] - '0';

        int output_position = 0;
        for (auto *out : m_outputs)
        {
            if (!out || !out->proxy) continue;

            log_verbose("KDE: <%d> (init) output '%s' uuid=%s enabled=%d modes=%d cap=0x%x\n",
                        m_id, out->name.c_str(), out->uuid.c_str(),
                        (int)out->enabled, (int)out->modes.size(), out->capabilities);

            if (out->name.empty())
                continue;

            // Only count enabled outputs with a current mode toward
            // output_position — mirrors xrandr's behaviour of only
            // counting connected outputs with a crtc. This way
            // "screen0" reliably picks the first *usable* output,
            // not the first output that happens to be advertised
            // (which might be disabled).
            if (!out->enabled || !out->current_mode)
                continue;

            bool name_match = (!strcmp(m_device_name, "auto") ||
                               !strcmp(m_device_name, out->name.c_str()) ||
                               output_position == screen_pos);
            if (name_match)
            {
                if (!chosen || !strcmp(m_device_name, "auto"))
                {
                    chosen = out;
                    if (strcmp(m_device_name, "auto") && screen_pos < 0)
                        break;   // explicit connector-name match - done
                }
            }
            output_position++;
        }

        // If we didn't find an enabled match, fall back to the first
        // name-matched output even if disabled — the caller may want
        // to enable it later. This preserves the original behavior
        // of matching by name for disabled outputs.
        if (!chosen)
        {
            for (auto *out : m_outputs)
            {
                if (!out || !out->proxy) continue;
                if (out->name.empty()) continue;
                if (!strcmp(m_device_name, "auto") ||
                    !strcmp(m_device_name, out->name.c_str()))
                {
                    chosen = out;
                    break;
                }
            }
        }

        if (chosen)
        {
            m_desktop_output = chosen;
            m_desktop_mode   = chosen->current_mode;   // snapshot for MODE_DESKTOP restore
            // Cache the desktop mode's timing too — if the
            // compositor destroys the proxy during set_custom_modes,
            // we can find the mode again by matching width/height/refresh.
            if (m_desktop_mode)
            {
                // Pass 1: cache the desktop mode's CVT timing.
                // Used by set_timing(MODE_DESKTOP) as a fallback
                // search key if the compositor destroys the
                // desktop mode proxy during a set_custom_modes
                // round-trip (full CVT match handles duplicate
                // resolutions with different timings, e.g. CVT
                // vs CVT-RB).
                for (const auto &mi : chosen->modes)
                {
                    if (mi.proxy == m_desktop_mode)
                    {
                        m_desktop_width       = mi.width;
                        m_desktop_height      = mi.height;
                        m_desktop_refresh_mhz = mi.refresh_mhz;
                        m_desktop_dot_clock_khz = mi.cvt_dot_clock_khz;
                        m_desktop_hsync_start   = mi.cvt_hsync_start;
                        m_desktop_hsync_end     = mi.cvt_hsync_end;
                        m_desktop_htotal        = mi.cvt_htotal;
                        m_desktop_vsync_start   = mi.cvt_vsync_start;
                        m_desktop_vsync_end     = mi.cvt_vsync_end;
                        m_desktop_vtotal        = mi.cvt_vtotal;
                        m_desktop_cvt_flags     = mi.cvt_flags;
                        break;
                    }
                }

                // Pass 2: preserve ALL custom modes the
                // compositor advertises for this output, not
                // just the desktop mode. The protocol's
                // set_custom_modes is a *replacement* operation
                // — without preserving every pre-existing
                // custom mode, our first add_mode would wipe
                // out custom modes the user set up via other
                // tools (KDE display settings, a previous
                // switchres run with keep_changes=true, another
                // Wayland client). We re-send this full list on
                // every rebuild, adding the user's own modes
                // on top.
                //
                // Each captured entry is marked is_preserved
                // so delete_mode() can't erase it (the user
                // didn't add it via us, so they shouldn't be
                // able to delete it via us either) and
                // add_mode()'s duplicate-check doesn't bail
                // out early when the user re-adds a mode that
                // happens to match a preserved one.
                for (const auto &mi : chosen->modes)
                {
                    if (!(mi.flags & 0x1)) continue;   // only custom modes

                    custom_mode_entry entry;
                    modeline_from_mode_info(&mi, &entry.ml);
                    entry.is_preserved = true;
                    m_custom_modes.push_back(entry);

                    if (mi.proxy == m_desktop_mode)
                        log_verbose("KDE: <%d> (init) desktop mode is custom, preserving\n", m_id);
                    else
                        log_verbose("KDE: <%d> (init) preserving pre-existing custom mode %ux%u@%.3f\n",
                                    m_id, mi.width, mi.height, mi.refresh_mhz / 1000.0);
                }
            }
            m_managed = 1;
            detected = true;
            log_verbose("KDE: <%d> (init) [SELECTED] output '%s' uuid=%s (desktop mode %p, %ux%u@%.3f)\n",
                         m_id, chosen->name.c_str(), chosen->uuid.c_str(),
                         (void *)m_desktop_mode, m_desktop_width, m_desktop_height,
                         m_desktop_refresh_mhz / 1000.0);
        }
    }

    if (!detected)
        log_error("KDE: <%d> (init) [ERROR] no screen detected\n", m_id);

    return detected;
}

// =========================================================================
//  pump_until_apply_done (single-threaded)
//  We drive event dispatch from the calling thread via wl_display_roundtrip.
//  This matches the xrandr backend's blocking XSync model: the caller blocks
//  until the compositor responds. No background thread, no condition
//  variable, no races. The mutex members remain in the class for the
//  listener thunks' lock_guards (which are uncontended here but keep the
//  code thread-safe if a dispatch thread is added later).
// =========================================================================

bool kde_timing::pump_until_apply_done()
{
	// Roundtrip until the apply listener flips m_apply_done. In practice a
	// single roundtrip suffices because the compositor processes requests
	// in order and replies before our sync-done arrives. The loop guards
	// against the unlikely case where one roundtrip dispatches other
	// events but not the apply response.
	for (int i = 0; i < 4 && !m_apply_done; i++)
	{
		if (wl_display_roundtrip(m_display) < 0)
		{
			log_error("KDE: <%d> (apply) [ERROR] wl_display_roundtrip failed\n", m_id);
			return false;
		}
	}
	if (!m_apply_done)
	{
		log_error("KDE: <%d> (apply) [ERROR] no apply response from compositor\n", m_id);
		return false;
	}
	if (!m_apply_ok)
	{
		log_error("KDE: <%d> (apply) [ERROR] configuration failed: %s\n",
			  m_id, m_apply_failure_reason.empty()
				 ? "(no reason given)"
				 : m_apply_failure_reason.c_str());
	}
	return m_apply_ok;
}

// =========================================================================
//  Registry listener (wl_registry: global / global_remove)
// =========================================================================

void kde_timing::registry_global(void *data, wl_registry *r,
				      uint32_t name, const char *iface, uint32_t version)
{
	auto *self = static_cast<kde_timing *>(data);
	if (!iface) return;

	log_verbose("KDE: <%d> (registry_global) iface=%s name=%u version=%u\n",
		    self->m_id, iface, name, version);

	if (strcmp(iface, "kde_output_device_registry_v2") == 0)
	{
		// Need >= v24 so that output proxies created via the `output`
		// event support the cvt event (full CVT timings on modes).
		// The output proxy version is determined by the registry's
		// bind version, so we check it here in registry_global.
		uint32_t v = version < 25 ? version : 25;
		if (v < 24)
		{
			log_error("KDE: <%d> (registry_global) [ERROR] kde_output_device_registry_v2 advertised at v%u (need >=24 for cvt event)\n",
				  self->m_id, v);
			return;
		}
		self->m_device_registry =
		    (kde_output_device_registry_v2 *)wl_registry_bind(r, name,
			&kde_output_device_registry_v2_interface, v);
	}
	else if (strcmp(iface, "kde_output_management_v2") == 0)
	{
		// Need >= v22 for kde_mode_list_v2.add_cvt (full CVT timing
		// submission for custom modes).
		uint32_t v = version < 25 ? version : 25;
		if (v < 22)
		{
			log_error("KDE: <%d> (registry_global) [ERROR] kde_output_management_v2 advertised at v%u (need >=22 for add_cvt)\n",
				  self->m_id, v);
			return;
		}
		self->m_management =
		    (kde_output_management_v2 *)wl_registry_bind(r, name,
			&kde_output_management_v2_interface, v);
	}
	// We don't bind kde_output_device_v2 directly here - we get output
	// proxies via the device registry's `output` event (see
	// device_registry_listener). Output proxies therefore have NO
	// wl_registry global name, and wl_registry.global_remove is never
	// fired for them - output removal is signalled by
	// kde_output_device_v2.removed instead.
}

void kde_timing::registry_global_remove(void * /*data*/, wl_registry * /*r*/,
					     uint32_t /*name*/)
{
	// No-op. The two globals we bind (kde_output_device_registry_v2 and
	// kde_output_management_v2) are persistent for the compositor's
	// lifetime and won't be removed during a switchres session. Output
	// device removal is signalled by kde_output_device_v2.removed (see
	// device_removed), NOT by wl_registry.global_remove — output proxies
	// come from the device registry's `output` event, not from
	// wl_registry_bind, so they have no wl_registry global name.
}

// =========================================================================
//  kde_output_device_registry_v2 listener: `finished` event (since v21)
//  The compositor sends this (as a destructor event) to signal that no more
//  output announcements will be made and the registry object is being
//  retired. After receiving it, the client should destroy its registry
//  proxy. Existing output proxies remain valid - they each have their own
//  `removed` event for cleanup.
// =========================================================================

void kde_timing::device_registry_finished(void *data,
					       kde_output_device_registry_v2 *reg)
{
	auto *self = static_cast<kde_timing *>(data);
	log_verbose("KDE: <%d> (device_registry_finished) registry retiring\n",
		    self->m_id);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	if (self->m_device_registry == reg)
		self->m_device_registry = nullptr;
	// The `finished` event is type="destructor" - the compositor destroys
	// the object after sending it, so we don't need to call destroy here.
}

// =========================================================================
//  kde_output_device_v2 listener thunks
// =========================================================================

// Helper to find our kde_output by its proxy. Caller must hold m_mutex.
kde_output *kde_timing::find_output_by_proxy_locked(
    kde_timing *self, kde_output_device_v2 *proxy)
{
	for (auto *out : self->m_outputs)
		if (out && out->proxy == proxy)
			return out;
	return nullptr;
}

void kde_timing::device_geometry(void *data, kde_output_device_v2 *dev,
				      int32_t x, int32_t y,
				      int32_t /*phys_w*/, int32_t /*phys_h*/,
				      int32_t /*subpixel*/, const char *make,
				      const char *model, int32_t transform)
{
	auto *self = static_cast<kde_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	auto *out = find_output_by_proxy_locked(self, dev);
	if (!out) return;
	out->x = x;
	out->y = y;
	out->transform = transform;
	if (make)  out->make  = make;
	if (model) out->model = model;
}

void kde_timing::device_current_mode(void *data, kde_output_device_v2 *dev,
					  kde_output_device_mode_v2 *mode)
{
	auto *self = static_cast<kde_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	auto *out = find_output_by_proxy_locked(self, dev);
	if (!out) return;
	out->current_mode = mode;
	for (auto &mi : out->modes)
		mi.current = (mi.proxy == mode);
}

void kde_timing::device_mode(void *data, kde_output_device_v2 *dev,
				 kde_output_device_mode_v2 *mode)
{
	auto *self = static_cast<kde_timing *>(data);
	if (!mode) return;
	std::lock_guard<std::mutex> lock(self->m_mutex);
	auto *out = find_output_by_proxy_locked(self, dev);
	if (!out) return;

	// Check if we already have this proxy (shouldn't happen, but guard).
	for (auto &mi : out->modes)
		if (mi.proxy == mode) return;

	kde_mode_info mi;
	mi.proxy = mode;
	mi.id = (int)out->modes.size();   // sequential ID (0, 1, 2, ...)
	out->modes.push_back(mi);

	// Attach the mode listener. User-data is `this`; we look up the
	// mode_info by proxy in each thunk.
	kde_output_device_mode_v2_add_listener(mode, &mode_listener, self);

	log_verbose("KDE: <%d> (device_mode) mode %p added (total: %zu)\n",
		self->m_id, (void *)mode, out->modes.size());
}

void kde_timing::device_done(void *data, kde_output_device_v2 *dev)
{
	auto *self = static_cast<kde_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	auto *out = find_output_by_proxy_locked(self, dev);
	if (!out) return;

	out->done_received = true;

	// Renumber all mode IDs to be sequential (0, 1, 2, ...) after the
	// mode list stabilizes. The compositor sends mode events (add +
	// remove) in a batch, then fires done(). After removals, the
	// vector may have gaps or non-sequential IDs (new modes got IDs
	// based on the vector size at insertion time, which included
	// soon-to-be-removed old modes). Renumbering here ensures
	// sr_get_mode(i) can find mode i for all i < modes.size().
	for (size_t i = 0; i < out->modes.size(); i++)
		out->modes[i].id = (int)i;

	log_verbose("KDE: <%d> (device_done) renumbered %zu mode(s)\n",
		    self->m_id, out->modes.size());
}

void kde_timing::device_scale(void * /*data*/, kde_output_device_v2 * /*dev*/,
				   wl_fixed_t /*scale*/)
{
	// We don't currently care about scale.
}

void kde_timing::device_edid(void * /*data*/, kde_output_device_v2 * /*dev*/,
				  const char * /*raw*/)
{
	// EDID is base64-encoded; switchres has its own EDID parser, but
	// plumbing it through here is left for a future patch.
}

void kde_timing::device_enabled(void *data, kde_output_device_v2 *dev,
				    int32_t enabled)
{
	auto *self = static_cast<kde_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	auto *out = find_output_by_proxy_locked(self, dev);
	if (out) out->enabled = (enabled != 0);
}

void kde_timing::device_uuid(void *data, kde_output_device_v2 *dev,
				  const char *uuid)
{
	auto *self = static_cast<kde_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	auto *out = find_output_by_proxy_locked(self, dev);
	if (out && uuid) out->uuid = uuid;
}

void kde_timing::device_serial_number(void *data, kde_output_device_v2 *dev,
					   const char *serial)
{
	auto *self = static_cast<kde_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	auto *out = find_output_by_proxy_locked(self, dev);
	if (out && serial) out->serial_number = serial;
}

void kde_timing::device_eisa_id(void *data, kde_output_device_v2 *dev,
				     const char *eisa)
{
	auto *self = static_cast<kde_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	auto *out = find_output_by_proxy_locked(self, dev);
	if (out && eisa) out->eisa_id = eisa;
}

void kde_timing::device_capabilities(void *data, kde_output_device_v2 *dev,
					 uint32_t flags)
{
	auto *self = static_cast<kde_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	auto *out = find_output_by_proxy_locked(self, dev);
	if (out) out->capabilities = flags;
}

void kde_timing::device_name(void *data, kde_output_device_v2 *dev,
				  const char *name)
{
	auto *self = static_cast<kde_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	auto *out = find_output_by_proxy_locked(self, dev);
	if (out && name) out->name = name;
}

void kde_timing::device_removed(void *data, kde_output_device_v2 *dev)
{
	auto *self = static_cast<kde_timing *>(data);
	if (!dev) return;

	// Per the protocol (kde_output_device_v2.removed, since v21):
	//   "The client should call the kde_output_device_v2.release
	//    request after receiving this event."
	// So unlike mode proxies (which the compositor destroys itself
	// after mode.removed), the DEVICE proxy is owned by the client
	// and we must call release/destroy on it ourselves.

	// First, drop our cache entry under the lock. We save the proxy
	// pointer locally so we can call release on it after releasing
	// the lock (release may queue a write and we don't want to hold
	// the mutex across libwayland internals).
	{
		std::lock_guard<std::mutex> lock(self->m_mutex);
		for (auto it = self->m_outputs.begin(); it != self->m_outputs.end(); ++it)
		{
			kde_output *out = *it;
			if (out && out->proxy == dev)
			{
				out->proxy = nullptr;   // mark gone so nobody else uses it
				if (self->m_desktop_output == out)
				{
					self->m_desktop_output = nullptr;
					self->m_desktop_mode   = nullptr;
				}
				// Clear any remaining mode cache entries. The
				// compositor SHOULD have sent mode.removed for
				// each already, but be defensive: if it didn't,
				// we drop our references here. We do NOT call
				// release on the mode proxies - the mode
				// interface has no release request, and the
				// compositor destroys them itself.
				out->modes.clear();
				delete out;
				self->m_outputs.erase(it);
				break;
			}
		}
	}

	// Now release the proxy itself. For v >= 21, use the `release`
	// destructor request (the documented protocol). For older
	// versions, fall back to wl_proxy_destroy via the generated
	// *_destroy wrapper. After this call `dev` is invalid.
	uint32_t v = kde_output_device_v2_get_version(dev);
	if (v >= 21)
		kde_output_device_v2_release(dev);
	else
		kde_output_device_v2_destroy(dev);
}

// =========================================================================
//  Unused event stubs (kde_output_device_v2 events we don't handle)
//  These are no-op functions with the exact signatures the protocol
//  expects. libwayland aborts if a listener slot is NULL, so every
//  event in the vtable must point to a real function — even if we
//  don't care about it.
// =========================================================================

#define SR_WAYLAND_STUB(name, params) \
    void kde_timing::name params { /* no-op */ }

SR_WAYLAND_STUB(device_overscan,        (void * /*data*/, kde_output_device_v2 * /*dev*/, uint32_t /*overscan*/))
SR_WAYLAND_STUB(device_vrr_policy,      (void * /*data*/, kde_output_device_v2 * /*dev*/, uint32_t /*vrr_policy*/))
SR_WAYLAND_STUB(device_rgb_range,       (void * /*data*/, kde_output_device_v2 * /*dev*/, uint32_t /*rgb_range*/))
SR_WAYLAND_STUB(device_high_dynamic_range, (void * /*data*/, kde_output_device_v2 * /*dev*/, uint32_t /*hdr_enabled*/))
SR_WAYLAND_STUB(device_sdr_brightness, (void * /*data*/, kde_output_device_v2 * /*dev*/, uint32_t /*sdr_brightness*/))
SR_WAYLAND_STUB(device_wide_color_gamut,(void * /*data*/, kde_output_device_v2 * /*dev*/, uint32_t /*wcg_enabled*/))
SR_WAYLAND_STUB(device_auto_rotate_policy,(void * /*data*/, kde_output_device_v2 * /*dev*/, uint32_t /*policy*/))
SR_WAYLAND_STUB(device_icc_profile_path,(void * /*data*/, kde_output_device_v2 * /*dev*/, const char * /*profile_path*/))
SR_WAYLAND_STUB(device_brightness_metadata,(void * /*data*/, kde_output_device_v2 * /*dev*/, uint32_t /*max_peak_brightness*/, uint32_t /*max_frame_average_brightness*/, uint32_t /*min_brightness*/))
SR_WAYLAND_STUB(device_brightness_overrides,(void * /*data*/, kde_output_device_v2 * /*dev*/, int32_t /*max_peak_brightness*/, int32_t /*max_average_brightness*/, int32_t /*min_brightness*/))
SR_WAYLAND_STUB(device_sdr_gamut_wideness,(void * /*data*/, kde_output_device_v2 * /*dev*/, uint32_t /*gamut_wideness*/))
SR_WAYLAND_STUB(device_color_profile_source,(void * /*data*/, kde_output_device_v2 * /*dev*/, uint32_t /*source*/))
SR_WAYLAND_STUB(device_brightness,      (void * /*data*/, kde_output_device_v2 * /*dev*/, uint32_t /*brightness*/))
SR_WAYLAND_STUB(device_color_power_tradeoff,(void * /*data*/, kde_output_device_v2 * /*dev*/, uint32_t /*preference*/))
SR_WAYLAND_STUB(device_dimming,         (void * /*data*/, kde_output_device_v2 * /*dev*/, uint32_t /*multiplier*/))
SR_WAYLAND_STUB(device_replication_source,(void * /*data*/, kde_output_device_v2 * /*dev*/, const char * /*source*/))
SR_WAYLAND_STUB(device_ddc_ci_allowed, (void * /*data*/, kde_output_device_v2 * /*dev*/, uint32_t /*allowed*/))
SR_WAYLAND_STUB(device_max_bits_per_color,(void * /*data*/, kde_output_device_v2 * /*dev*/, uint32_t /*max_bpc*/))
SR_WAYLAND_STUB(device_max_bits_per_color_range,(void * /*data*/, kde_output_device_v2 * /*dev*/, uint32_t /*min_value*/, uint32_t /*max_value*/))
SR_WAYLAND_STUB(device_automatic_max_bits_per_color_limit,(void * /*data*/, kde_output_device_v2 * /*dev*/, uint32_t /*max_bpc_limit*/))
SR_WAYLAND_STUB(device_edr_policy,     (void * /*data*/, kde_output_device_v2 * /*dev*/, uint32_t /*policy*/))
SR_WAYLAND_STUB(device_sharpness,       (void * /*data*/, kde_output_device_v2 * /*dev*/, uint32_t /*sharpness*/))
SR_WAYLAND_STUB(device_priority,        (void * /*data*/, kde_output_device_v2 * /*dev*/, uint32_t /*priority*/))
SR_WAYLAND_STUB(device_auto_brightness, (void * /*data*/, kde_output_device_v2 * /*dev*/, uint32_t /*enabled*/))
SR_WAYLAND_STUB(device_hdr_icc_profile_path,(void * /*data*/, kde_output_device_v2 * /*dev*/, const char * /*profile_path*/))
SR_WAYLAND_STUB(device_hdr_color_profile_source,(void * /*data*/, kde_output_device_v2 * /*dev*/, uint32_t /*source*/))
SR_WAYLAND_STUB(device_abm_level,       (void * /*data*/, kde_output_device_v2 * /*dev*/, uint32_t /*level*/))

#undef SR_WAYLAND_STUB

// =========================================================================
//  kde_output_device_mode_v2 listener thunks
// =========================================================================

// Find a kde_mode_info by its proxy across all outputs. Caller must
// hold m_mutex. Returns nullptr if not found.
kde_mode_info *kde_timing::find_mode_info_locked(
    kde_timing *self, kde_output_device_mode_v2 *proxy)
{
	for (auto *out : self->m_outputs)
	{
		if (!out) continue;
		for (auto &mi : out->modes)
			if (mi.proxy == proxy) return &mi;
	}
	return nullptr;
}

void kde_timing::mode_size(void *data, kde_output_device_mode_v2 *mode,
			       int32_t w, int32_t h)
{
	auto *self = static_cast<kde_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	auto *mi = find_mode_info_locked(self, mode);
	if (mi) { mi->width = (uint32_t)w; mi->height = (uint32_t)h; }
}

void kde_timing::mode_refresh(void *data, kde_output_device_mode_v2 *mode,
				  int32_t refresh_mhz)
{
	auto *self = static_cast<kde_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	auto *mi = find_mode_info_locked(self, mode);
	if (mi) mi->refresh_mhz = refresh_mhz;
}

void kde_timing::mode_preferred(void *data, kde_output_device_mode_v2 *mode)
{
	auto *self = static_cast<kde_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	auto *mi = find_mode_info_locked(self, mode);
	if (mi) mi->preferred = true;
}

void kde_timing::mode_removed(void *data, kde_output_device_mode_v2 *mode)
{
	auto *self = static_cast<kde_timing *>(data);
	// Save the proxy pointer — we need it after clearing our cache to
	// destroy the client-side proxy object. The mode.removed event
	// is NOT type="destructor", so libwayland does NOT automatically
	// free the wl_proxy. We must call destroy() ourselves to avoid
	// a transient memory leak (the proxy would otherwise stay
	// allocated until wl_display_disconnect).
	kde_output_device_mode_v2 *proxy_to_destroy = mode;

	{
		std::lock_guard<std::mutex> lock(self->m_mutex);
		if (self->m_desktop_mode == mode)
			self->m_desktop_mode = nullptr;
		log_verbose("KDE: <%d> (mode_removed) mode %p being removed\n",
		    self->m_id, (void *)mode);
		for (auto *out : self->m_outputs)
		{
			if (!out) continue;
			for (auto it = out->modes.begin(); it != out->modes.end(); ++it)
			{
				if (it->proxy == mode)
				{
					if (out->current_mode == mode)
						out->current_mode = nullptr;
					out->modes.erase(it);
					break;
				}
			}
		}
	}

	// Destroy the client-side proxy object. This frees the wl_proxy
	// struct in libwayland. Safe to call here because:
	// 1. mode.removed is NOT type="destructor", so libwayland won't
	//    also destroy it (no double-free risk)
	// 2. We've already removed it from our cache, so no other code
	//    will use the pointer
	// 3. The compositor has already destroyed its side (or will
	//    immediately after this event), so this just cleans up ours
	if (proxy_to_destroy)
		kde_output_device_mode_v2_destroy(proxy_to_destroy);
}

void kde_timing::mode_flags(void *data, kde_output_device_mode_v2 *mode,
				uint32_t flags)
{
	auto *self = static_cast<kde_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	auto *mi = find_mode_info_locked(self, mode);
	if (mi) mi->flags = flags;
}

void kde_timing::mode_cvt(void *data, kde_output_device_mode_v2 *mode,
			      uint32_t dot_clock_khz,
			      uint32_t hdisplay, uint32_t hsync_start, uint32_t hsync_end,
			      uint32_t htotal, uint32_t hskew,
			      uint32_t vdisplay, uint32_t vsync_start, uint32_t vsync_end,
			      uint32_t vtotal, uint32_t vscan, uint32_t flags)
{
	auto *self = static_cast<kde_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	auto *mi = find_mode_info_locked(self, mode);
	if (!mi) return;
	mi->has_cvt = true;
	mi->cvt_dot_clock_khz = dot_clock_khz;
	mi->cvt_hdisplay = hdisplay;  mi->cvt_hsync_start = hsync_start;
	mi->cvt_hsync_end = hsync_end; mi->cvt_htotal = htotal; mi->cvt_hskew = hskew;
	mi->cvt_vdisplay = vdisplay;  mi->cvt_vsync_start = vsync_start;
	mi->cvt_vsync_end = vsync_end; mi->cvt_vtotal = vtotal; mi->cvt_vscan = vscan;
	mi->cvt_flags = flags;
}

// =========================================================================
//  kde_output_configuration_v2 listener thunks
// =========================================================================

void kde_timing::cfg_applied(void *data, kde_output_configuration_v2 * /*cfg*/)
{
	auto *self = static_cast<kde_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	self->m_apply_done = true;
	self->m_apply_ok   = true;
	log_verbose("KDE: <%d> (cfg_applied) configuration applied\n", self->m_id);
}

void kde_timing::cfg_failed(void *data, kde_output_configuration_v2 * /*cfg*/)
{
	auto *self = static_cast<kde_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	self->m_apply_done = true;
	self->m_apply_ok   = false;
	log_verbose("KDE: <%d> (cfg_failed) configuration rejected\n", self->m_id);
}

void kde_timing::cfg_failure_reason(void *data,
					kde_output_configuration_v2 * /*cfg*/,
					const char *reason)
{
	auto *self = static_cast<kde_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	if (reason) self->m_apply_failure_reason = reason;
}

// =========================================================================
//  Helper: drm_flags_from_modeline
// =========================================================================

uint32_t kde_timing::drm_flags_from_modeline(const modeline *m)
{
	uint32_t flags = 0;
	if (m->interlace)  flags |= KDE_DRM_MODE_FLAG_INTERLACE;
	if (m->doublescan) flags |= KDE_DRM_MODE_FLAG_DBLSCAN;
	if (m->hsync)      flags |= KDE_DRM_MODE_FLAG_PHSYNC;
	else               flags |= KDE_DRM_MODE_FLAG_NHSYNC;
	if (m->vsync)      flags |= KDE_DRM_MODE_FLAG_PVSYNC;
	else               flags |= KDE_DRM_MODE_FLAG_NVSYNC;
	return flags;
}

// =========================================================================
//  Helper: modeline_from_mode_info
//  Populates a modeline struct from a cached kde_mode_info.
//  Caller should already zero-init `out`.
// =========================================================================

void kde_timing::modeline_from_mode_info(const kde_mode_info *mi,
					     modeline *out)
{
	out->platform_data = (uintptr_t)mi->proxy;
	out->id = mi->id;

	if (mi->has_cvt)
	{
		out->pclock  = (uint64_t)mi->cvt_dot_clock_khz * 1000;  // kHz -> Hz
		out->hactive = (int)mi->cvt_hdisplay;
		out->hbegin  = (int)mi->cvt_hsync_start;
		out->hend    = (int)mi->cvt_hsync_end;
		out->htotal  = (int)mi->cvt_htotal;
		out->vactive = (int)mi->cvt_vdisplay;
		out->vbegin  = (int)mi->cvt_vsync_start;
		out->vend    = (int)mi->cvt_vsync_end;
		out->vtotal  = (int)mi->cvt_vtotal;
		out->interlace  = (mi->cvt_flags & KDE_DRM_MODE_FLAG_INTERLACE) ? 1 : 0;
		out->doublescan = (mi->cvt_flags & KDE_DRM_MODE_FLAG_DBLSCAN)   ? 1 : 0;
		out->hsync      = (mi->cvt_flags & KDE_DRM_MODE_FLAG_PHSYNC)    ? 1 : 0;
		out->vsync      = (mi->cvt_flags & KDE_DRM_MODE_FLAG_PVSYNC)    ? 1 : 0;
		if (out->htotal > 0)
			out->hfreq = out->pclock / out->htotal;
		if (out->vtotal > 0)
			out->vfreq = out->hfreq / out->vtotal * (out->interlace ? 2 : 1);
		out->refresh = out->vfreq;
		out->width  = (int)mi->cvt_hdisplay;
		out->height = (int)mi->cvt_vdisplay;
	}
	else
	{
		// Compositor didn't send CVT (protocol < v24, or non-custom mode).
		// Fill in what we have.
		out->width  = (int)mi->width;
		out->height = (int)mi->height;
		out->vfreq  = mi->refresh_mhz / 1000.0;
		out->refresh = out->vfreq;
	}

	out->type |= CUSTOM_VIDEO_TIMING_KDE;
}

// =========================================================================
//  Helper: find_output_by_name
// =========================================================================

bool kde_timing::find_output_by_name(const char *name, kde_output *&out)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	for (auto *o : m_outputs)
	{
		if (!o) continue;
		if (!strcmp(name, "auto"))
		{
			if (o->enabled && o->current_mode) { out = o; return true; }
		}
		else if (o->name == name)
		{
			out = o; return true;
		}
	}
	return false;
}

// =========================================================================
//  Helper: find_mode_by_proxy
// =========================================================================

kde_mode_info *kde_timing::find_mode_by_proxy(
    kde_output_device_mode_v2 *proxy)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return find_mode_info_locked(this, proxy);
}

// =========================================================================
//  Helper: find_advertised_mode_for
//  Match a requested modeline against currently-advertised modes by
//  exact CVT equality. Used after set_custom_modes+apply to find which
//  proxy the compositor created for our request.
// =========================================================================

kde_mode_info *kde_timing::find_advertised_mode_for(const modeline *m)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!m_desktop_output) return nullptr;

	uint32_t want_dot_khz = (uint32_t)(m->pclock / 1000);
	uint32_t want_flags   = drm_flags_from_modeline(m);

	for (auto &mi : m_desktop_output->modes)
	{
		// Prefer "ours" (custom-flagged) modes when matching.
		if (!mi.has_cvt) continue;
		if (mi.cvt_dot_clock_khz == want_dot_khz &&
		    mi.cvt_hdisplay == (uint32_t)m->hactive &&
		    mi.cvt_hsync_start == (uint32_t)m->hbegin &&
		    mi.cvt_hsync_end == (uint32_t)m->hend &&
		    mi.cvt_htotal == (uint32_t)m->htotal &&
		    mi.cvt_vdisplay == (uint32_t)m->vactive &&
		    mi.cvt_vsync_start == (uint32_t)m->vbegin &&
		    mi.cvt_vsync_end == (uint32_t)m->vend &&
		    mi.cvt_vtotal == (uint32_t)m->vtotal &&
		    mi.cvt_flags == want_flags)
		{
			return &mi;
		}
	}
	return nullptr;
}

// =========================================================================
//  rebuild_custom_modes_and_apply
//  Re-sends the full custom-mode list via set_custom_modes and waits for
//  the compositor's apply response. This is the heart of add_mode /
//  delete_mode - the protocol replaces the whole list, so we maintain
//  m_custom_modes as the source of truth and re-send it each time.
// =========================================================================

bool kde_timing::rebuild_custom_modes_and_apply()
{
	if (!m_management || !m_desktop_output)
	{
		log_error("KDE: <%d> (rebuild) [ERROR] no management/output\n", m_id);
		return false;
	}
	if (!(m_desktop_output->capabilities & KDE_CAP_CUSTOM_MODES))
	{
		log_error("KDE: <%d> (rebuild) [ERROR] compositor does not advertise custom_modes capability\n", m_id);
		return false;
	}

	// Build the mode list.
	kde_mode_list_v2 *list = kde_output_management_v2_create_mode_list(m_management);
	if (!list)
	{
		log_error("KDE: <%d> (rebuild) [ERROR] create_mode_list failed\n", m_id);
		return false;
	}

	for (const auto &entry : m_custom_modes)
	{
		const modeline &m = entry.ml;
		// add_cvt is self-contained: it carries the full CVT timing
		// (dot clock, h/v sync ranges, h/v totals, flags) and adds
		// the mode in one call. The set_resolution / set_refresh_rate
		// / set_reduced_blanking / add_mode sequence is the OTHER
		// (simpler) path the protocol offers for when you only have
		// width/height/refresh and want the compositor to generate the
		// timings. Switchres always has full CVT timings, so we use
		// only add_cvt here.
		kde_mode_list_v2_add_cvt(list,
					 (uint32_t)(m.pclock / 1000),
					 (uint32_t)m.hactive,
					 (uint32_t)m.hbegin,
					 (uint32_t)m.hend,
					 (uint32_t)m.htotal,
					 0,                       // hskew
					 (uint32_t)m.vactive,
					 (uint32_t)m.vbegin,
					 (uint32_t)m.vend,
					 (uint32_t)m.vtotal,
					 0,                       // vscan
					 drm_flags_from_modeline(&m));
	}

	// Build the configuration and attach the listener BEFORE sending apply.
	kde_output_configuration_v2 *cfg =
	    kde_output_management_v2_create_configuration(m_management);
	if (!cfg)
	{
		kde_mode_list_v2_destroy(list);
		log_error("KDE: <%d> (rebuild) [ERROR] create_configuration failed\n", m_id);
		return false;
	}

	// Reset apply state.
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_apply_done = false;
		m_apply_ok = false;
		m_apply_failure_reason.clear();
	}

	kde_output_configuration_v2_add_listener(cfg, &configuration_listener, this);
	kde_output_configuration_v2_set_custom_modes(cfg,
						      m_desktop_output->proxy, list);

	// We're done with the list - the compositor copies it.
	kde_mode_list_v2_destroy(list);

	// Send apply and flush.
	kde_output_configuration_v2_apply(cfg);
	wl_display_flush(m_display);

	bool ok = pump_until_apply_done();

	// Give the dispatch thread a chance to receive the new `mode` events
	// that the compositor sends after a successful set_custom_modes.
	if (ok)
		wl_display_roundtrip(m_display);

	kde_output_configuration_v2_destroy(cfg);
	return ok;
}

// =========================================================================
//  send_apply_for_mode_switch
//  Builds a configuration that switches `device` to `target_proxy` and
//  waits for the apply response. Position/transform are passed through
//  but optional (use current values if caller doesn't care).
// =========================================================================

bool kde_timing::send_apply_for_mode_switch(
    kde_output_device_mode_v2 *target_proxy, int x, int y, int transform)
{
	if (!m_management || !m_desktop_output || !target_proxy)
		return false;

	kde_output_configuration_v2 *cfg =
	    kde_output_management_v2_create_configuration(m_management);
	if (!cfg) return false;

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_apply_done = false;
		m_apply_ok = false;
		m_apply_failure_reason.clear();
	}

	kde_output_configuration_v2_add_listener(cfg, &configuration_listener, this);
	kde_output_configuration_v2_mode(cfg, m_desktop_output->proxy, target_proxy);

	if (transform >= 0)
		kde_output_configuration_v2_transform(cfg, m_desktop_output->proxy, transform);
	if (x >= 0 && y >= 0)
		kde_output_configuration_v2_position(cfg, m_desktop_output->proxy, x, y);

	kde_output_configuration_v2_apply(cfg);
	wl_display_flush(m_display);

	bool ok = pump_until_apply_done();
	kde_output_configuration_v2_destroy(cfg);

	if (ok)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_desktop_output->current_mode = target_proxy;
		for (auto &mi : m_desktop_output->modes)
			mi.current = (mi.proxy == target_proxy);
	}
	return ok;
}

// =========================================================================
//  add_mode
// =========================================================================

bool kde_timing::add_mode(modeline *mode)
{
	if (!mode) return false;
	if (!m_desktop_output)
	{
		log_error("KDE: <%d> (add_mode) [ERROR] no screen detected\n", m_id);
		return false;
	}
	if (!m_managed)
	{
		log_error("KDE: <%d> (add_mode) [WARNING] this screen is not managed by us\n", m_id);
		return false;
	}
	if (!(m_desktop_output->capabilities & KDE_CAP_CUSTOM_MODES))
	{
		log_error("KDE: <%d> (add_mode) [ERROR] compositor does not advertise custom_modes capability\n", m_id);
		return false;
	}

	// Bail if an identical custom mode is already registered.
    // Skip is_preserved entries — we don't want a user's "add 600x500"
    // to bail out early just because the compositor happened to be
    // advertising a 600x500 custom mode that we captured at init. The
    // user-added entry will live alongside the preserved one (the
    // duplicate-check semantics are about "did WE already add this",
    // not "does this already exist in the compositor's list").
    for (const auto &e : m_custom_modes)
    {
        if (e.is_preserved) continue;
        if (e.proxy &&
            e.ml.hactive == mode->hactive && e.ml.vactive == mode->vactive &&
            e.ml.pclock  == mode->pclock  && e.ml.htotal == mode->htotal &&
            e.ml.vtotal  == mode->vtotal)
        {
            log_error("KDE: <%d> (add_mode) [WARNING] mode already registered\n", m_id);
            mode->platform_data = (uintptr_t)e.proxy;
            return true;
        }
    }

	log_verbose("KDE: <%d> (add_mode) %dx%d@%.3f pclock=%llu\n",
		    m_id, mode->hactive, mode->vactive, mode->vfreq,
		    (unsigned long long)mode->pclock);

	custom_mode_entry entry;
	entry.ml = *mode;
	entry.proxy = nullptr;

	m_custom_modes.push_back(entry);

	bool ok = rebuild_custom_modes_and_apply();
	if (!ok)
	{
		// Roll back the optimistic append.
		m_custom_modes.pop_back();
		mode->type |= MODE_ERROR;
		return false;
	}

	// After a successful apply, the compositor has emitted new `mode`
	// events on the device. Match the freshly-created proxy by CVT
	// equality and store it.
	kde_mode_info *mi = find_advertised_mode_for(mode);
	if (mi)
	{
		mi->is_ours = true;
		mode->platform_data = (uintptr_t)mi->proxy;
		mode->id = mi->id;   // update id to match the new mode's position
		m_custom_modes.back().proxy = mi->proxy;
		mode->type |= CUSTOM_VIDEO_TIMING_KDE;
		log_verbose("KDE: <%d> (add_mode) mode matched to proxy %p, id=%d\n",
			m_id, (void *)mi->proxy, mi->id);
		return true;
	}

	log_error("KDE: <%d> (add_mode) [ERROR] apply succeeded but no matching advertised mode found\n", m_id);
	mode->type |= MODE_ERROR;
	return false;
}

// =========================================================================
//  delete_mode
// =========================================================================

bool kde_timing::delete_mode(modeline *mode)
{
	if (!mode) return false;
	if (!m_desktop_output)
	{
		log_error("KDE: <%d> (delete_mode) [ERROR] no screen detected\n", m_id);
		return false;
	}
	if (!m_managed)
	{
		log_error("KDE: <%d> (delete_mode) [WARNING] this screen is not managed by us\n", m_id);
		return false;
	}

	// If the mode is currently active, switch back to the desktop mode first.
	if (mode->platform_data &&
	    (kde_output_device_mode_v2 *)mode->platform_data == m_desktop_output->current_mode)
	{
		log_verbose("KDE: <%d> (delete_mode) [WARNING] mode currently active, restoring desktop mode first\n", m_id);
		modeline desktop_mode = {};
		desktop_mode.type |= MODE_DESKTOP;
		if (!set_timing(&desktop_mode))
		{
			log_error("KDE: <%d> (delete_mode) [ERROR] could not restore desktop mode\n", m_id);
			return false;
		}
	}

	// Find and remove the entry from our custom list.
    // Skip is_preserved entries — these are modes the compositor was
    // advertising at init time (the desktop mode if it's custom, plus
    // any other pre-existing custom modes). The user didn't add them
    // via us, so they shouldn't be able to delete them via us either.
    // Without this skip, the wrapper's deinit cleanup walking
    // video_modes would erase preserved entries and the next rebuild
    // would send a list missing them, causing the compositor to
    // delete them — wiping out the user's pre-existing custom mode
    // configuration (including potentially the desktop mode itself).
    bool found = false;
    for (auto it = m_custom_modes.begin(); it != m_custom_modes.end(); ++it)
    {
        if (it->is_preserved) continue;   // never delete preserved entries
        if (it->proxy == (kde_output_device_mode_v2 *)mode->platform_data)
        {
            m_custom_modes.erase(it);
            found = true;
            break;
        }
    }
    if (!found)
    {
        // Nothing was erased — either the caller asked us to delete a
        // native mode that was never in our custom list, or to delete
        // a preserved entry (which we deliberately skip above). In
        // either case the custom-mode list is unchanged, so sending
        // set_custom_modes again would just make the compositor
        // destroy and re-create every custom mode (proxy churn) for
        // no reason. Skip the rebuild.
        log_verbose("KDE: <%d> (delete_mode) mode not in our user-added custom list (skipped, no rebuild)\n", m_id);
        return true;
    }

	bool ok = rebuild_custom_modes_and_apply();
	if (ok)
	{
		mode->platform_data = 0;
		log_verbose("KDE: <%d> (delete_mode) mode removed\n", m_id);
	}
	return ok;
}

// =========================================================================
//  update_mode
// =========================================================================

bool kde_timing::update_mode(modeline *mode)
{
	if (!mode) return false;
	if (!delete_mode(mode))
	{
		log_error("KDE: <%d> (update_mode) [ERROR] delete failed\n", m_id);
		return false;
	}
	if (!add_mode(mode))
	{
		log_error("KDE: <%d> (update_mode) [ERROR] add failed\n", m_id);
		return false;
	}
	return true;
}

// =========================================================================
//  set_timing
// =========================================================================

bool kde_timing::set_timing(modeline *mode)
{
	if (!mode) return false;
	if (!m_desktop_output)
	{
		log_error("KDE: <%d> (set_timing) [ERROR] no screen detected\n", m_id);
		return false;
	}
	if (!m_managed)
	{
		log_error("KDE: <%d> (set_timing) [WARNING] this screen is not managed by us\n", m_id);
		return false;
	}

	kde_output_device_mode_v2 *target = nullptr;

	if (mode->type & MODE_DESKTOP)
	{
		// Restore the desktop mode = the mode that was current at init()
		// time. We snapshotted it into m_desktop_mode so that switching
		// to custom modes in between doesn't lose the original. This
		// mirrors the xrandr backend's m_desktop_mode handling.
		std::lock_guard<std::mutex> lock(m_mutex);
		target = m_desktop_mode;
		if (!target)
		{
			// The desktop mode proxy was destroyed (e.g. by a
			// set_custom_modes call that removed it, or the
			// compositor garbage-collected it). Try to find it
			// again in the current mode list by matching the
			// cached width/height/refresh.
			if (m_desktop_output && (m_desktop_width || m_desktop_height))
			{
				log_verbose("KDE: <%d> (set_timing) desktop mode proxy gone, searching by %ux%u@%.3f\n",
					    m_id, m_desktop_width, m_desktop_height,
					    m_desktop_refresh_mhz / 1000.0);
				for (const auto &mi : m_desktop_output->modes)
				{
					// Try full CVT match first (handles
					// duplicate resolutions with
					// different timings, e.g. CVT vs
					// CVT-RB).
					if (mi.has_cvt &&
					    mi.cvt_dot_clock_khz == m_desktop_dot_clock_khz &&
					    mi.cvt_hsync_start == m_desktop_hsync_start &&
					    mi.cvt_hsync_end == m_desktop_hsync_end &&
					    mi.cvt_htotal == m_desktop_htotal &&
					    mi.cvt_vsync_start == m_desktop_vsync_start &&
					    mi.cvt_vsync_end == m_desktop_vsync_end &&
					    mi.cvt_vtotal == m_desktop_vtotal &&
					    mi.cvt_flags == m_desktop_cvt_flags)
					{
						target = mi.proxy;
						log_verbose("KDE: <%d> (set_timing) found desktop mode by full CVT: %p\n",
							    m_id, (void *)target);
						break;
					}
					// Fall back to width/height/refresh
					// (for compositors that don't send
					// the cvt event).
					if (mi.width == m_desktop_width &&
					    mi.height == m_desktop_height &&
					    mi.refresh_mhz == m_desktop_refresh_mhz)
					{
						target = mi.proxy;
						log_verbose("KDE: <%d> (set_timing) found desktop mode by w/h/r: %p\n",
							    m_id, (void *)target);
						break;
					}
				}
			}
			if (!target)
			{
				// Last resort: fall back to the live current
				// mode (which is whatever we last switched to).
				target = m_desktop_output ? m_desktop_output->current_mode : nullptr;
			}
		}
		if (!target)
		{
			log_error("KDE: <%d> (set_timing) [ERROR] no desktop mode to restore\n", m_id);
			return false;
		}
	}
	else
	{
		target = (kde_output_device_mode_v2 *)mode->platform_data;
		if (target)
		{
			// Verify the mode is still advertised.
			if (!find_mode_by_proxy(target))
			{
				log_verbose("KDE: <%d> (set_timing) proxy %p not in current list, searching by id=%d w=%d h=%d r=%d\n",
					m_id, (void *)target, mode->id, mode->width, mode->height, mode->refresh);
				target = nullptr;   // fall through to search
			}
		}
		if (!target)
		{
			// The proxy was destroyed (e.g. by a set_custom_modes
			// that replaced the mode list). Search the current
			// mode list by modeline::id, or by width/height/refresh.
			if (m_desktop_output)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				for (const auto &mi : m_desktop_output->modes)
				{
					// Try id match first
					if (mode->id != 0 && mi.id == mode->id)
					{
						target = mi.proxy;
						log_verbose("KDE: <%d> (set_timing) found mode by id=%d: %p\n",
							m_id, mi.id, (void *)target);
						break;
					}
					// Try full CVT match (handles
					// duplicate resolutions with
					// different timings).
					if (mi.has_cvt &&
					    mi.cvt_dot_clock_khz == (uint32_t)(mode->pclock / 1000) &&
					    mi.cvt_hsync_start == (uint32_t)mode->hbegin &&
					    mi.cvt_hsync_end == (uint32_t)mode->hend &&
					    mi.cvt_htotal == (uint32_t)mode->htotal &&
					    mi.cvt_vsync_start == (uint32_t)mode->vbegin &&
					    mi.cvt_vsync_end == (uint32_t)mode->vend &&
					    mi.cvt_vtotal == (uint32_t)mode->vtotal)
					{
						target = mi.proxy;
						log_verbose("KDE: <%d> (set_timing) found mode by full CVT: %p\n",
							m_id, (void *)target);
						break;
					}
					// Fall back to width/height/refresh
					if (mi.width == (uint32_t)mode->width &&
					    mi.height == (uint32_t)mode->height &&
					    (int)(mi.refresh_mhz / 1000) == (int)mode->refresh)
					{
						target = mi.proxy;
						log_verbose("KDE: <%d> (set_timing) found mode by w/h/r: %p\n",
							m_id, (void *)target);
						break;
					}
				}
			}
			if (!target)
			{
				log_error("KDE: <%d> (set_timing) [ERROR] mode not found in current advertisement\n", m_id);
				return false;
			}
		}
	}

	log_verbose("KDE: <%d> (set_timing) applying mode %p\n", m_id, (void *)target);
	bool ok = send_apply_for_mode_switch(target,
					      m_desktop_output->x,
					      m_desktop_output->y,
					      m_desktop_output->transform);
	if (ok)
		log_verbose("KDE: <%d> (set_timing) applied\n", m_id);
	return ok;
}

// =========================================================================
//  get_timing
// =========================================================================

bool kde_timing::get_timing(modeline *mode)
{
    if (!m_desktop_output)
    {
        log_error("KDE: <%d> (get_timing) [ERROR] no screen detected\n", m_id);
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    log_verbose("KDE: <%d> (get_timing) position=%d, mode count=%zu\n",
        m_id, m_video_modes_position, m_desktop_output->modes.size());

    if ((size_t)m_video_modes_position < m_desktop_output->modes.size())
    {
        const kde_mode_info &mi = m_desktop_output->modes[m_video_modes_position];
        memset(mode, 0, sizeof(*mode));
        modeline_from_mode_info(&mi, mode);

        // Tag the desktop mode. We can't rely on the proxy pointer
        // alone — set_custom_modes destroys and re-creates all mode
        // proxies (native and custom), so m_desktop_mode becomes stale
        // after the first add_mode. Match by the cached desktop CVT or
        // w/h/refresh, which are stable across set_custom_modes.
        bool is_desktop = (m_desktop_mode == mi.proxy);
        if (!is_desktop && m_desktop_width && m_desktop_height && mi.has_cvt)
        {
            bool cvt_match = (mi.cvt_dot_clock_khz == m_desktop_dot_clock_khz &&
                mi.cvt_hsync_start == m_desktop_hsync_start &&
                mi.cvt_hsync_end  == m_desktop_hsync_end &&
                mi.cvt_htotal     == m_desktop_htotal &&
                mi.cvt_vsync_start == m_desktop_vsync_start &&
                mi.cvt_vsync_end  == m_desktop_vsync_end &&
                mi.cvt_vtotal     == m_desktop_vtotal &&
                mi.cvt_flags      == m_desktop_cvt_flags);

            bool whr_match = (mi.width == m_desktop_width &&
                mi.height == m_desktop_height &&
                mi.refresh_mhz == m_desktop_refresh_mhz);

            is_desktop = cvt_match || whr_match;
        }
        if (is_desktop)
            mode->type |= MODE_DESKTOP;

        log_verbose("KDE: <%d> (get_timing) returning mode %p %ux%u@%.3f type=0x%x\n",
            m_id, (void *)mi.proxy, mi.width, mi.height,
            mi.refresh_mhz / 1000.0, mode->type);
        m_video_modes_position++;
    }
    else
    {
        // List exhausted; reset the cursor (mirrors xrandr's behaviour).
        log_verbose("KDE: <%d> (get_timing) list exhausted, resetting cursor\n", m_id);
        m_video_modes_position = 0;
    }
    return true;
}

// =========================================================================
//  process_modelist
// =========================================================================

bool kde_timing::process_modelist(std::vector<modeline *> modelist)
{
	bool error = false;
	bool result = false;

	for (auto &mode : modelist)
	{
		if (mode->type & MODE_DELETE)
			result = delete_mode(mode);
		else if (mode->type & MODE_ADD)
			result = add_mode(mode);
		else if (mode->type & MODE_UPDATE)
			result = update_mode(mode);

		if (!result)
		{
			mode->type |= MODE_ERROR;
			error = true;
		}
		else
		{
			mode->type &= ~MODE_ERROR;
		}
	}
	return !error;
}
