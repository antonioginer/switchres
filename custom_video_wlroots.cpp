/**************************************************************

   custom_video_wlroots.cpp - Linux Wayland (wlroots)
                                video management layer

   ---------------------------------------------------------

   Switchres   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2021 Chris Kennedy, Antonio Giner,
			 Alexandre Wodarczyk, Gil Delescluse
   Copyright   2026 Mareks Rops

   ---------------------------------------------------------

   See custom_video_wlroots.h for design notes.

   Threading model
   ---------------
   Wayland is asynchronous; switchres' custom_video API is synchronous.
   We bridge the two with wl_display_roundtrip() called from the public
   API methods themselves (single-threaded design, matching the KDE
   backend). All listener callbacks run on the calling thread during
   the roundtrip and serialise on m_mutex. The mutex is uncontended in
   this design but is kept so a background dispatch thread can be
   added later without rewriting the thunks.

 **************************************************************/

#include <stdio.h>
#include <exception>
#include <string.h>
#include <chrono>

#include "custom_video_wlroots.h"
#include "log.h"

// =========================================================================
//  Compile the wayland-scanner-generated protocol source file directly
//  into this translation unit. The generated .c defines the interface
//  metadata structs (e.g. zwlr_output_manager_v1_interface) and the
//  request marshalling code, all of which are referenced by the inline
//  wrappers in the generated header. Including it here (wrapped in
//  extern "C") keeps the makefile simple - no separate .c -> .o build
//  rule, no extra objects on the link line. The generated header has
//  include guards so the double inclusion (via custom_video_wlroots.h
//  above and via the .c below) is harmless.
//
//  The generated code marks interface structs with WL_PRIVATE
//  (__attribute__((visibility("hidden"))), which triggers
//  -Wattributes warnings when compiled as C++ without
//  -fvisibility=hidden. Suppress those warnings locally - they're
//  cosmetic; the structs work fine with default visibility in a
//  static build.
// =========================================================================
#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wattributes"
#endif
extern "C" {
#include "wlr-output-management-unstable-v1-client-protocol.c"
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
//  declaration order. libwayland aborts if any slot is NULL, so every
//  event gets a real function pointer - even events we don't care
//  about are wired to no-op stubs.
// =========================================================================

static const wl_registry_listener registry_listener = {
	wlroots_timing::registry_global,
	wlroots_timing::registry_global_remove
};

// zwlr_output_manager_v1: 3 events in XML declaration order.
//   head, done, finished
static const zwlr_output_manager_v1_listener manager_listener = {
	wlroots_timing::manager_head,
	wlroots_timing::manager_done,
	wlroots_timing::manager_finished
};

// zwlr_output_head_v1: 14 events in XML declaration order.
//   name, description, physical_size, mode, enabled, current_mode,
//   position, transform, scale, finished, make(v2), model(v2),
//   serial_number(v2), adaptive_sync(v4)
static const zwlr_output_head_v1_listener head_listener = {
	wlroots_timing::head_name,
	wlroots_timing::head_description,
	wlroots_timing::head_physical_size,
	wlroots_timing::head_mode,
	wlroots_timing::head_enabled,
	wlroots_timing::head_current_mode,
	wlroots_timing::head_position,
	wlroots_timing::head_transform,
	wlroots_timing::head_scale,
	wlroots_timing::head_finished,
	wlroots_timing::head_make,
	wlroots_timing::head_model,
	wlroots_timing::head_serial_number,
	wlroots_timing::head_adaptive_sync
};

// zwlr_output_mode_v1: 4 events in XML declaration order.
//   size, refresh, preferred, finished
static const zwlr_output_mode_v1_listener mode_listener = {
	wlroots_timing::mode_size,
	wlroots_timing::mode_refresh,
	wlroots_timing::mode_preferred,
	wlroots_timing::mode_finished
};

// zwlr_output_configuration_v1: 3 events in XML declaration order.
//   succeeded, failed, cancelled
static const zwlr_output_configuration_v1_listener configuration_listener = {
	wlroots_timing::cfg_succeeded,
	wlroots_timing::cfg_failed,
	wlroots_timing::cfg_cancelled
};

// =========================================================================
//  Constructor
// =========================================================================

wlroots_timing::wlroots_timing(char *device_name, custom_video_settings *vs)
{
	m_vs = *vs;
	m_id = ++s_id;

	log_verbose("WLROOTS: <%d> (wlroots_timing) creation (%s)\n", m_id, device_name);

	// Copy device name with the same truncation behaviour as xrandr.
	if ((strlen(device_name) + 1) > 32)
	{
		strncpy(m_device_name, device_name, 31);
		log_error("WLROOTS: <%d> (wlroots_timing) [ERROR] device name truncated to %s\n", m_id, m_device_name);
	}
	else
		strcpy(m_device_name, device_name);

	// Connect to the Wayland display. wl_display_connect returns NULL on
	// failure (no compositor, WAYLAND_DISPLAY unset, etc.).
	m_display = wl_display_connect(NULL);
	if (!m_display)
	{
		log_verbose("WLROOTS: <%d> (wlroots_timing) Wayland server not found\n", m_id);
		throw std::exception();
	}

	// Acquire the registry; we'll bind the manager global from it.
	m_registry = wl_display_get_registry(m_display);
	if (!m_registry)
	{
		log_error("WLROOTS: <%d> (wlroots_timing) [ERROR] wl_display_get_registry failed\n", m_id);
		wl_display_disconnect(m_display);
		m_display = nullptr;
		throw std::exception();
	}

	wl_registry_add_listener(m_registry, &registry_listener, this);

	// One roundtrip to bind zwlr_output_manager_v1 (registry_global runs
	// here).
	wl_display_roundtrip(m_display);

	if (!m_manager)
	{
		log_error("WLROOTS: <%d> (wlroots_timing) [ERROR] compositor does not advertise zwlr_output_manager_v1\n", m_id);
		throw std::exception();
	}

	// Now attach the manager listener. It will start emitting `head`
	// events, each creating a zwlr_output_head_v1 proxy. The head proxy
	// then emits name/mode/enabled/current_mode/etc events, and each mode
	// proxy emits size/refresh/preferred events.
	zwlr_output_manager_v1_add_listener(m_manager, &manager_listener, this);

	// Two roundtrips to drain the initial burst of head/mode events so
	// that init() sees a fully-populated cache. The compositor processes
	// events in order: bind -> head* -> mode* (per head) -> done. A
	// single roundtrip usually suffices; the second one handles the
	// case where mode sub-events arrive after the manager's done event.
	wl_display_roundtrip(m_display);
	wl_display_roundtrip(m_display);

	s_total_managed_screen++;
}

// =========================================================================
//  Destructor
// =========================================================================

wlroots_timing::~wlroots_timing()
{
	s_total_managed_screen--;
	if (s_total_managed_screen == 0)
		s_id = 0;

	// Single-threaded design: no dispatch thread to stop. Release
	// Wayland resources in reverse order of acquisition.
	release_all_outputs();

	if (m_manager)
	{
		// The manager has no explicit destroy/release request. Calling
		// stop() tells the compositor we're done; it will send
		// `finished` (a destructor event) which libwayland uses to
		// free the proxy. We don't need to wait for it - disconnect
		// will clean up regardless.
		zwlr_output_manager_v1_stop(m_manager);
	}
	if (m_registry)
		wl_registry_destroy(m_registry);
	if (m_display)
		wl_display_disconnect(m_display);
}

// =========================================================================
//  release_all_outputs
// =========================================================================

void wlroots_timing::release_all_outputs()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	for (auto *out : m_outputs)
	{
		if (!out) continue;
		if (out->proxy)
		{
			// Use the release destructor request (since v3). For older
			// versions fall back to the generic destroy wrapper.
			uint32_t v = zwlr_output_head_v1_get_version(out->proxy);
			if (v >= 3)
				zwlr_output_head_v1_release(out->proxy);
			else
				zwlr_output_head_v1_destroy(out->proxy);
		}
		// Mode proxies are released individually in mode_finished (the
		// compositor sends finished for each when the head goes away).
		// If it didn't, be defensive and release them here.
		for (auto &mi : out->modes)
		{
			if (mi.proxy)
			{
				uint32_t mv = zwlr_output_mode_v1_get_version(mi.proxy);
				if (mv >= 3)
					zwlr_output_mode_v1_release(mi.proxy);
				else
					zwlr_output_mode_v1_destroy(mi.proxy);
			}
		}
		delete out;
	}
	m_outputs.clear();
	m_desktop_output = nullptr;
	m_desktop_mode = nullptr;
}

// =========================================================================
//  init()
// =========================================================================

bool wlroots_timing::init()
{
    log_verbose("WLROOTS: <%d> (init) waiting for compositor state to settle\n", m_id);

    // Make sure initial head/mode events have been processed.
    wl_display_roundtrip(m_display);
    wl_display_roundtrip(m_display);

    bool detected = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        log_verbose("WLROOTS: <%d> (init) %d head(s) discovered\n",
                    m_id, (int)m_outputs.size());

        // Diagnostic: print mode count for each head BEFORE selection
        for (auto *out : m_outputs)
        {
            if (!out || !out->proxy) continue;
            log_verbose("WLROOTS: <%d> (init) head '%s' has %zu mode(s), enabled=%d, current_mode=%p\n",
                m_id, out->name.c_str(), out->modes.size(),
                (int)out->enabled, (void *)out->current_mode);
            for (size_t i = 0; i < out->modes.size(); i++)
            {
                const auto &mi = out->modes[i];
                log_verbose("WLROOTS: <%d> (init)   [%zu] %p %ux%u@%.3f\n",
                    m_id, i, (void *)mi.proxy, mi.width, mi.height,
                    mi.refresh_mhz / 1000.0);
            }
        }

        wlroots_output *chosen = nullptr;

        // Parse the device name. Three forms are accepted, matching
        // the xrandr backend's conventions:
        //   - "auto"          : pick the first enabled output
        //   - connector name  : e.g. "eDP-1", "DP-1", "HDMI-A-1"
        //   - "screen0".."screen9" or "0".."9" : the Nth output by
        //                       index (the switchres manager's
        //                       display index as a string)
        // The screen-position form is what groovy mame (and the
        // switchres C wrapper's default) passes — without it, the
        // wlroots backend would fail with "no screen detected"
        // whenever the caller doesn't explicitly use "auto" or a
        // connector name.
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

            log_verbose("WLROOTS: <%d> (init) head '%s' enabled=%d modes=%d\n",
                        m_id, out->name.c_str(),
                        (int)out->enabled, (int)out->modes.size());

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
            // Cache the desktop mode's w/h/refresh so we can find the mode
            // again by matching even if the proxy is destroyed.
            if (m_desktop_mode)
            {
                for (const auto &mi : chosen->modes)
                {
                    if (mi.proxy == m_desktop_mode)
                    {
                        m_desktop_width       = mi.width;
                        m_desktop_height      = mi.height;
                        m_desktop_refresh_mhz = mi.refresh_mhz;
                        break;
                    }
                }
            }
            m_managed = 1;
            detected = true;
            log_verbose("WLROOTS: <%d> (init) [SELECTED] head '%s' (desktop mode %p, %ux%u@%.3f)\n",
                         m_id, chosen->name.c_str(),
                         (void *)m_desktop_mode, m_desktop_width, m_desktop_height,
                         m_desktop_refresh_mhz / 1000.0);
        }
    }

    if (!detected)
        log_error("WLROOTS: <%d> (init) [ERROR] no screen detected\n", m_id);

    return detected;
}

// =========================================================================
//  pump_until_apply_done (single-threaded)
//  We drive event dispatch from the calling thread via wl_display_roundtrip.
//  This matches the xrandr/KDE backend's blocking model: the caller blocks
//  until the compositor responds. No background thread, no condition
//  variable, no races.
// =========================================================================

bool wlroots_timing::pump_until_apply_done()
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
			log_error("WLROOTS: <%d> (apply) [ERROR] wl_display_roundtrip failed\n", m_id);
			return false;
		}
	}
	if (!m_apply_done)
	{
		log_error("WLROOTS: <%d> (apply) [ERROR] no apply response from compositor\n", m_id);
		return false;
	}
	if (!m_apply_ok)
	{
		if (m_apply_cancelled)
			log_error("WLROOTS: <%d> (apply) [ERROR] configuration cancelled (stale serial)\n", m_id);
		else
			log_error("WLROOTS: <%d> (apply) [ERROR] configuration failed: %s\n",
				  m_id, m_apply_failure_reason.empty()
					 ? "(no reason given)"
					 : m_apply_failure_reason.c_str());
	}
	return m_apply_ok;
}

// =========================================================================
//  Registry listener (wl_registry: global / global_remove)
// =========================================================================

void wlroots_timing::registry_global(void *data, wl_registry *r,
				      uint32_t name, const char *iface, uint32_t version)
{
	auto *self = static_cast<wlroots_timing *>(data);
	if (!iface) return;

	log_verbose("WLROOTS: <%d> (registry_global) iface=%s name=%u version=%u\n",
		    self->m_id, iface, name, version);

	if (strcmp(iface, "zwlr_output_manager_v1") == 0)
	{
		// Need >= v4 for the full feature set (the manager's own v4
		// features aren't used directly, but binding at v4 gives us
		// v4 head/configuration/configuration_head objects, which
		// matters for release requests on heads and for the
		// adaptive_sync event on heads).
		uint32_t v = version < 4 ? version : 4;
		if (v < 4)
		{
			log_error("WLROOTS: <%d> (registry_global) [ERROR] zwlr_output_manager_v1 advertised at v%u (need >=4)\n",
				  self->m_id, v);
			return;
		}
		self->m_manager =
		    (zwlr_output_manager_v1 *)wl_registry_bind(r, name,
			&zwlr_output_manager_v1_interface, v);
	}
	// We don't bind anything else here. Heads and modes come from the
	// manager's events, not from wl_registry_bind.
}

void wlroots_timing::registry_global_remove(void * /*data*/, wl_registry * /*r*/,
					     uint32_t /*name*/)
{
	// No-op. The only global we bind (zwlr_output_manager_v1) is
	// persistent for the compositor's lifetime and won't be removed
	// during a switchres session. Head removal is signalled by
	// zwlr_output_head_v1.finished, NOT by wl_registry.global_remove.
}

// =========================================================================
//  zwlr_output_manager_v1 listener thunks
// =========================================================================

void wlroots_timing::manager_head(void *data, zwlr_output_manager_v1 * /*mgr*/,
				   zwlr_output_head_v1 *head)
{
	auto *self = static_cast<wlroots_timing *>(data);
	if (!head) return;
	std::lock_guard<std::mutex> lock(self->m_mutex);

	// Guard against duplicate head events (shouldn't happen, but be safe).
	for (auto *out : self->m_outputs)
		if (out && out->proxy == head) return;

	auto *out = new wlroots_output();
	out->proxy = head;

	zwlr_output_head_v1_add_listener(head, &head_listener, self);
	self->m_outputs.push_back(out);
	log_verbose("WLROOTS: <%d> (manager_head) new head proxy %p (total: %zu)\n",
		    self->m_id, (void *)head, self->m_outputs.size());
}

void wlroots_timing::manager_done(void *data, zwlr_output_manager_v1 * /*mgr*/,
				   uint32_t serial)
{
	auto *self = static_cast<wlroots_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);

	// Stash the serial for the next create_configuration call.
	self->m_serial = serial;

	// Renumber all mode IDs sequentially (0, 1, 2, ...) per head after
	// the mode list stabilises. The compositor sends mode events (add
	// via head.mode + remove via mode.finished) in a batch, then fires
	// done. After removals, a head's mode vector may have gaps or
	// non-sequential IDs (new modes got IDs based on the vector size at
	// insertion time, which included soon-to-be-removed old modes).
	// Renumbering here ensures sr_get_mode(i) can find mode i for all
	// i < modes.size(). This matches the KDE backend's device_done
	// behaviour.
	for (auto *out : self->m_outputs)
	{
		if (!out) continue;
		out->done_received = true;
		for (size_t i = 0; i < out->modes.size(); i++)
			out->modes[i].id = (int)i;
	}

	log_verbose("WLROOTS: <%d> (manager_done) serial=%u, renumbered modes\n",
		    self->m_id, serial);
}

void wlroots_timing::manager_finished(void *data, zwlr_output_manager_v1 *mgr)
{
	auto *self = static_cast<wlroots_timing *>(data);
	log_verbose("WLROOTS: <%d> (manager_finished) manager retiring\n",
		    self->m_id);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	if (self->m_manager == mgr)
		self->m_manager = nullptr;
	// The `finished` event is type="destructor" - the compositor destroys
	// the object after sending it, so we don't need to call destroy here.
}

// =========================================================================
//  zwlr_output_head_v1 listener thunks
// =========================================================================

// Helper to find our wlroots_output by its head proxy. Caller must hold m_mutex.
wlroots_output *wlroots_timing::find_output_by_proxy_locked(
    wlroots_timing *self, zwlr_output_head_v1 *proxy)
{
	for (auto *out : self->m_outputs)
		if (out && out->proxy == proxy)
			return out;
	return nullptr;
}

void wlroots_timing::head_name(void *data, zwlr_output_head_v1 *head,
			       const char *name)
{
	auto *self = static_cast<wlroots_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	auto *out = find_output_by_proxy_locked(self, head);
	if (out && name) out->name = name;
}

void wlroots_timing::head_description(void *data, zwlr_output_head_v1 *head,
				       const char *description)
{
	auto *self = static_cast<wlroots_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	auto *out = find_output_by_proxy_locked(self, head);
	if (out && description) out->description = description;
}

void wlroots_timing::head_physical_size(void *data, zwlr_output_head_v1 *head,
					 int32_t width, int32_t height)
{
	auto *self = static_cast<wlroots_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	auto *out = find_output_by_proxy_locked(self, head);
	if (out)
	{
		out->physical_width_mm  = width;
		out->physical_height_mm = height;
	}
}

void wlroots_timing::head_mode(void *data, zwlr_output_head_v1 *head,
			       zwlr_output_mode_v1 *mode)
{
	auto *self = static_cast<wlroots_timing *>(data);
	if (!mode) return;
	std::lock_guard<std::mutex> lock(self->m_mutex);
	auto *out = find_output_by_proxy_locked(self, head);
	if (!out) return;

	// Check if we already have this proxy (shouldn't happen, but guard).
	for (auto &mi : out->modes)
		if (mi.proxy == mode) return;

	wlroots_mode_info mi;
	mi.proxy = mode;
	mi.id = (int)out->modes.size();   // sequential ID (will be renumbered on done)
	out->modes.push_back(mi);

	// Attach the mode listener. User-data is `this`; we look up the
	// mode_info by proxy in each thunk.
	zwlr_output_mode_v1_add_listener(mode, &mode_listener, self);

	log_verbose("WLROOTS: <%d> (head_mode) mode %p added (total: %zu)\n",
		self->m_id, (void *)mode, out->modes.size());
}

void wlroots_timing::head_enabled(void *data, zwlr_output_head_v1 *head,
				   int32_t enabled)
{
	auto *self = static_cast<wlroots_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	auto *out = find_output_by_proxy_locked(self, head);
	if (out) out->enabled = (enabled != 0);
}

void wlroots_timing::head_current_mode(void *data, zwlr_output_head_v1 *head,
					zwlr_output_mode_v1 *mode)
{
	auto *self = static_cast<wlroots_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	auto *out = find_output_by_proxy_locked(self, head);
	if (!out) return;
	out->current_mode = mode;
	for (auto &mi : out->modes)
		mi.current = (mi.proxy == mode);
}

void wlroots_timing::head_position(void *data, zwlr_output_head_v1 *head,
				    int32_t x, int32_t y)
{
	auto *self = static_cast<wlroots_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	auto *out = find_output_by_proxy_locked(self, head);
	if (out) { out->x = x; out->y = y; }
}

void wlroots_timing::head_transform(void *data, zwlr_output_head_v1 *head,
				     int32_t transform)
{
	auto *self = static_cast<wlroots_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	auto *out = find_output_by_proxy_locked(self, head);
	if (out) out->transform = transform;
}

void wlroots_timing::head_scale(void *data, zwlr_output_head_v1 *head,
				wl_fixed_t scale)
{
	auto *self = static_cast<wlroots_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	auto *out = find_output_by_proxy_locked(self, head);
	if (out) out->scale = scale;
}

void wlroots_timing::head_finished(void *data, zwlr_output_head_v1 *head)
{
	auto *self = static_cast<wlroots_timing *>(data);
	if (!head) return;

	// Per the protocol (zwlr_output_head_v1.finished):
	//   "The head object becomes inert. Clients should send a destroy
	//    request and release any resources associated with it."
	// Unlike the KDE backend's device.removed, the finished event is
	// NOT type="destructor", so libwayland does NOT automatically free
	// the proxy. We must call release/destroy ourselves.

	// First, drop our cache entry under the lock. Save the proxy
	// pointer so we can release it after releasing the lock.
	{
		std::lock_guard<std::mutex> lock(self->m_mutex);
		for (auto it = self->m_outputs.begin(); it != self->m_outputs.end(); ++it)
		{
			wlroots_output *out = *it;
			if (out && out->proxy == head)
			{
				out->proxy = nullptr;   // mark gone so nobody else uses it
				if (self->m_desktop_output == out)
				{
					self->m_desktop_output = nullptr;
					self->m_desktop_mode   = nullptr;
				}
				// Clear any remaining mode cache entries. The compositor
				// SHOULD have sent mode.finished for each already, but be
				// defensive: if it didn't, we drop our references here.
				// We do NOT call release on the mode proxies here - they
				// may already have been released in mode_finished, and
				// double-release is a protocol error. The compositor
				// cleans up its side when the head goes inert.
				out->modes.clear();
				delete out;
				self->m_outputs.erase(it);
				break;
			}
		}
	}

	// Now release the proxy itself. For v >= 3, use the `release`
	// destructor request. For older versions, fall back to the generic
	// destroy wrapper. After this call `head` is invalid.
	uint32_t v = zwlr_output_head_v1_get_version(head);
	if (v >= 3)
		zwlr_output_head_v1_release(head);
	else
		zwlr_output_head_v1_destroy(head);
}

void wlroots_timing::head_make(void *data, zwlr_output_head_v1 *head,
			       const char *make)
{
	auto *self = static_cast<wlroots_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	auto *out = find_output_by_proxy_locked(self, head);
	if (out && make) out->make = make;
}

void wlroots_timing::head_model(void *data, zwlr_output_head_v1 *head,
				 const char *model)
{
	auto *self = static_cast<wlroots_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	auto *out = find_output_by_proxy_locked(self, head);
	if (out && model) out->model = model;
}

void wlroots_timing::head_serial_number(void *data, zwlr_output_head_v1 *head,
					 const char *serial)
{
	auto *self = static_cast<wlroots_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	auto *out = find_output_by_proxy_locked(self, head);
	if (out && serial) out->serial_number = serial;
}

void wlroots_timing::head_adaptive_sync(void * /*data*/,
					 zwlr_output_head_v1 * /*head*/,
					 uint32_t /*state*/)
{
	// We don't currently care about adaptive sync (VRR) state.
}

// =========================================================================
//  zwlr_output_mode_v1 listener thunks
// =========================================================================

// Find a wlroots_mode_info by its proxy across all heads. Caller must
// hold m_mutex. Returns nullptr if not found.
wlroots_mode_info *wlroots_timing::find_mode_info_locked(
    wlroots_timing *self, zwlr_output_mode_v1 *proxy)
{
	for (auto *out : self->m_outputs)
	{
		if (!out) continue;
		for (auto &mi : out->modes)
			if (mi.proxy == proxy) return &mi;
	}
	return nullptr;
}

void wlroots_timing::mode_size(void *data, zwlr_output_mode_v1 *mode,
			       int32_t w, int32_t h)
{
	auto *self = static_cast<wlroots_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	auto *mi = find_mode_info_locked(self, mode);
	if (mi) { mi->width = (uint32_t)w; mi->height = (uint32_t)h; }
}

void wlroots_timing::mode_refresh(void *data, zwlr_output_mode_v1 *mode,
				  int32_t refresh_mhz)
{
	auto *self = static_cast<wlroots_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	auto *mi = find_mode_info_locked(self, mode);
	if (mi) mi->refresh_mhz = refresh_mhz;
}

void wlroots_timing::mode_preferred(void *data, zwlr_output_mode_v1 *mode)
{
	auto *self = static_cast<wlroots_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	auto *mi = find_mode_info_locked(self, mode);
	if (mi) mi->preferred = true;
}

void wlroots_timing::mode_finished(void *data, zwlr_output_mode_v1 *mode)
{
	auto *self = static_cast<wlroots_timing *>(data);
	// Save the proxy pointer - we need it after clearing our cache to
	// release the client-side proxy object. The mode.finished event is
	// NOT type="destructor", so libwayland does NOT automatically free
	// the wl_proxy. We must call release/destroy ourselves to avoid a
	// transient memory leak.
	zwlr_output_mode_v1 *proxy_to_release = mode;

	{
		std::lock_guard<std::mutex> lock(self->m_mutex);
		if (self->m_desktop_mode == mode)
			self->m_desktop_mode = nullptr;
		log_verbose("WLROOTS: <%d> (mode_finished) mode %p being removed\n",
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

	// Release the client-side proxy object. Safe to call here because:
	// 1. mode.finished is NOT type="destructor", so libwayland won't also
	//    free it (no double-free risk)
	// 2. We've already removed it from our cache, so no other code will
	//    use the pointer
	// 3. The compositor has already destroyed its side (or will
	//    immediately after this event), so this just cleans up ours
	if (proxy_to_release)
	{
		uint32_t v = zwlr_output_mode_v1_get_version(proxy_to_release);
		if (v >= 3)
			zwlr_output_mode_v1_release(proxy_to_release);
		else
			zwlr_output_mode_v1_destroy(proxy_to_release);
	}
}

// =========================================================================
//  zwlr_output_configuration_v1 listener thunks
// =========================================================================

void wlroots_timing::cfg_succeeded(void *data,
				    zwlr_output_configuration_v1 * /*cfg*/)
{
	auto *self = static_cast<wlroots_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	self->m_apply_done      = true;
	self->m_apply_ok        = true;
	self->m_apply_cancelled = false;
	log_verbose("WLROOTS: <%d> (cfg_succeeded) configuration applied\n", self->m_id);
}

void wlroots_timing::cfg_failed(void *data,
				 zwlr_output_configuration_v1 * /*cfg*/)
{
	auto *self = static_cast<wlroots_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	self->m_apply_done      = true;
	self->m_apply_ok        = false;
	self->m_apply_cancelled = false;
	log_verbose("WLROOTS: <%d> (cfg_failed) configuration rejected\n", self->m_id);
}

void wlroots_timing::cfg_cancelled(void *data,
				    zwlr_output_configuration_v1 * /*cfg*/)
{
	// Sent if the compositor cancels the configuration because the state
	// of an output changed and the client has outdated information (e.g.
	// after an output has been hot-plugged). The serial we used in
	// create_configuration is stale. The client should re-read the
	// configuration (roundtrip to get a fresh done event with a new
	// serial) and try again.
	auto *self = static_cast<wlroots_timing *>(data);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	self->m_apply_done      = true;
	self->m_apply_ok        = false;
	self->m_apply_cancelled = true;
	log_verbose("WLROOTS: <%d> (cfg_cancelled) serial was stale\n", self->m_id);
}

// =========================================================================
//  Helper: modeline_from_mode_info
//  Populates a modeline struct from a cached wlroots_mode_info.
//  The wlroots protocol only exposes width/height/refresh (no CVT
//  timings), so the resulting modeline has width/height/vfreq/refresh
//  populated but pclock/h*/v* are zero. Caller should zero-init `out`.
// =========================================================================

void wlroots_timing::modeline_from_mode_info(const wlroots_mode_info *mi,
					     modeline *out)
{
	out->platform_data = (uintptr_t)mi->proxy;
	out->id = mi->id;

	out->width  = (int)mi->width;
	out->height = (int)mi->height;
	out->hactive = out->width;
	out->vactive = out->height;
	out->vfreq   = mi->refresh_mhz / 1000.0;
	out->refresh = out->vfreq;

	out->type |= CUSTOM_VIDEO_TIMING_WLROOTS;
}

// =========================================================================
//  Helper: modeline_from_custom_entry
//  Returns the full modeline switchres generated (with CVT timings) for
//  one of our cached custom modes. platform_data is set to 0 so set_timing
//  knows to use set_custom_mode instead of set_mode.
// =========================================================================

void wlroots_timing::modeline_from_custom_entry(const custom_mode_entry *e,
						modeline *out)
{
	*out = e->ml;                         // copy the full modeline
	out->platform_data = 0;               // no proxy - signals custom mode
	out->type |= CUSTOM_VIDEO_TIMING_WLROOTS;
}

// =========================================================================
//  Helper: find_output_by_name
// =========================================================================

bool wlroots_timing::find_output_by_name(const char *name, wlroots_output *&out)
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

wlroots_mode_info *wlroots_timing::find_mode_by_proxy(
    zwlr_output_mode_v1 *proxy)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return find_mode_info_locked(this, proxy);
}

// =========================================================================
//  Helper: find_custom_mode_for
//  Match a requested modeline against our cached custom modes. Tries an
//  exact CVT match first (pclock + h/v totals), then falls back to
//  width/height/refresh.
// =========================================================================

wlroots_timing::custom_mode_entry *wlroots_timing::find_custom_mode_for(
    const modeline *m)
{
	for (auto &e : m_custom_modes)
	{
		if (e.ml.hactive == m->hactive && e.ml.vactive == m->vactive &&
		    e.ml.pclock  == m->pclock  && e.ml.htotal == m->htotal &&
		    e.ml.vtotal  == m->vtotal)
		{
			return &e;
		}
	}
	// Fall back to width/height/refresh (handles the case where switchres
	// regenerated the modeline with slightly different timings).
	int32_t want_mhz = (int32_t)(m->vfreq * 1000.0);
	for (auto &e : m_custom_modes)
	{
		if (e.ml.width  == m->width &&
		    e.ml.height == m->height &&
		    (int32_t)(e.ml.vfreq * 1000.0) == want_mhz)
		{
			return &e;
		}
	}
	return nullptr;
}

// =========================================================================
//  apply_configuration_for_mode
//  Builds a configuration that switches the desktop output to either an
//  advertised mode (target_proxy != nullptr, uses set_mode) or a custom
//  mode (target_proxy == nullptr, uses set_custom_mode with the given
//  w/h/refresh_mhz). Position/transform are passed through. Handles the
//  `cancelled` (stale serial) response by roundtripping to get a fresh
//  serial and retrying, up to 3 attempts.
// =========================================================================

bool wlroots_timing::apply_configuration_for_mode(
    zwlr_output_mode_v1 *target_proxy,
    int custom_w, int custom_h, int custom_refresh_mhz,
    int x, int y, int transform)
{
	if (!m_manager || !m_desktop_output)
	{
		log_error("WLROOTS: <%d> (apply) [ERROR] no manager/output\n", m_id);
		return false;
	}

	for (int attempt = 0; attempt < 3; attempt++)
	{
		uint32_t serial = 0;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			serial = m_serial;
		}

		zwlr_output_configuration_v1 *cfg =
		    zwlr_output_manager_v1_create_configuration(m_manager, serial);
		if (!cfg)
		{
			log_error("WLROOTS: <%d> (apply) [ERROR] create_configuration failed\n", m_id);
			return false;
		}

		// Reset apply state.
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_apply_done      = false;
			m_apply_ok        = false;
			m_apply_cancelled = false;
			m_apply_failure_reason.clear();
		}

		zwlr_output_configuration_v1_add_listener(cfg, &configuration_listener, this);

		// enable_head creates a zwlr_output_configuration_head_v1 object.
		// It's destroyed implicitly when the configuration is destroyed.
		zwlr_output_configuration_head_v1 *head_cfg =
		    zwlr_output_configuration_v1_enable_head(cfg, m_desktop_output->proxy);
		if (!head_cfg)
		{
			zwlr_output_configuration_v1_destroy(cfg);
			log_error("WLROOTS: <%d> (apply) [ERROR] enable_head failed\n", m_id);
			return false;
		}

		if (target_proxy)
			zwlr_output_configuration_head_v1_set_mode(head_cfg, target_proxy);
		else
			zwlr_output_configuration_head_v1_set_custom_mode(head_cfg,
			    custom_w, custom_h, custom_refresh_mhz);

		if (transform >= 0)
			zwlr_output_configuration_head_v1_set_transform(head_cfg, transform);
		if (x >= 0 && y >= 0)
			zwlr_output_configuration_head_v1_set_position(head_cfg, x, y);

		zwlr_output_configuration_v1_apply(cfg);
		wl_display_flush(m_display);

		bool ok = pump_until_apply_done();

		// The protocol says the client should destroy the configuration
		// after receiving succeeded/failed/cancelled. The destroy
		// request also frees the configuration_head objects.
		zwlr_output_configuration_v1_destroy(cfg);

		if (ok)
		{
			// Update our cached current_mode and active-custom tracking.
			std::lock_guard<std::mutex> lock(m_mutex);
			if (target_proxy)
			{
				m_desktop_output->current_mode = target_proxy;
				for (auto &mi : m_desktop_output->modes)
					mi.current = (mi.proxy == target_proxy);
				m_active_custom_w = 0;
				m_active_custom_h = 0;
				m_active_custom_refresh_mhz = 0;
			}
			else
			{
				// Custom mode - no proxy. Mark all advertised modes as
				// non-current and remember which custom mode is active.
				m_desktop_output->current_mode = nullptr;
				for (auto &mi : m_desktop_output->modes)
					mi.current = false;
				m_active_custom_w = custom_w;
				m_active_custom_h = custom_h;
				m_active_custom_refresh_mhz = custom_refresh_mhz;
			}
			return true;
		}

		// If cancelled, the serial was stale. Roundtrip to receive a
		// fresh `done` event (which updates m_serial), then retry.
		bool was_cancelled = false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			was_cancelled = m_apply_cancelled;
		}
		if (was_cancelled)
		{
			log_verbose("WLROOTS: <%d> (apply) serial stale (cancelled), roundtripping for fresh serial (attempt %d)\n",
				    m_id, attempt + 1);
			wl_display_roundtrip(m_display);
			continue;
		}

		// Failed (not cancelled) - don't retry, the configuration itself
		// was rejected.
		return false;
	}

	log_error("WLROOTS: <%d> (apply) [ERROR] gave up after 3 attempts (serial kept going stale)\n", m_id);
	return false;
}

// =========================================================================
//  add_mode
//  Adds a custom mode to our local cache. The wlroots protocol does NOT
//  persist custom modes between configurations, so we don't actually send
//  anything to the compositor here - the mode is applied on demand via
//  set_custom_mode when set_timing is called.
// =========================================================================

bool wlroots_timing::add_mode(modeline *mode)
{
	if (!mode) return false;
	if (!m_desktop_output)
	{
		log_error("WLROOTS: <%d> (add_mode) [ERROR] no screen detected\n", m_id);
		return false;
	}
	if (!m_managed)
	{
		log_error("WLROOTS: <%d> (add_mode) [WARNING] this screen is not managed by us\n", m_id);
		return false;
	}

	// Bail if an identical custom mode is already registered.
	for (const auto &e : m_custom_modes)
	{
		if (e.ml.hactive == mode->hactive && e.ml.vactive == mode->vactive &&
		    e.ml.pclock  == mode->pclock  && e.ml.htotal == mode->htotal &&
		    e.ml.vtotal  == mode->vtotal)
		{
			log_error("WLROOTS: <%d> (add_mode) [WARNING] mode already registered\n", m_id);
			mode->platform_data = 0;   // custom modes have no proxy
			return true;
		}
	}

	log_verbose("WLROOTS: <%d> (add_mode) %dx%d@%.3f pclock=%llu\n",
		    m_id, mode->hactive, mode->vactive, mode->vfreq,
		    (unsigned long long)mode->pclock);

	custom_mode_entry entry;
	entry.ml = *mode;
	m_custom_modes.push_back(entry);

	// No apply needed - wlroots doesn't pre-register custom modes. The
	// mode will be applied on demand via set_timing -> set_custom_mode.
	mode->platform_data = 0;   // signals "custom mode" to set_timing
	mode->type |= CUSTOM_VIDEO_TIMING_WLROOTS;
	return true;
}

// =========================================================================
//  delete_mode
//  Removes a custom mode from our local cache. If the mode is currently
//  active, we restore the desktop mode first (because the wlroots
//  protocol doesn't let us "switch away" from a custom mode - we just
//  apply a different configuration).
// =========================================================================

bool wlroots_timing::delete_mode(modeline *mode)
{
	if (!mode) return false;
	if (!m_desktop_output)
	{
		log_error("WLROOTS: <%d> (delete_mode) [ERROR] no screen detected\n", m_id);
		return false;
	}
	if (!m_managed)
	{
		log_error("WLROOTS: <%d> (delete_mode) [WARNING] this screen is not managed by us\n", m_id);
		return false;
	}

	// If the mode being deleted is the currently-active custom mode,
	// restore the desktop mode first. Custom modes have no proxy, so
	// we identify the active custom mode by w/h/refresh.
	bool is_active = false;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_active_custom_w == mode->width &&
		    m_active_custom_h == mode->height &&
		    m_active_custom_refresh_mhz == (int32_t)(mode->vfreq * 1000.0))
		{
			is_active = true;
		}
	}
	if (is_active)
	{
		log_verbose("WLROOTS: <%d> (delete_mode) [WARNING] mode currently active, restoring desktop mode first\n", m_id);
		modeline desktop_mode = {};
		desktop_mode.type |= MODE_DESKTOP;
		if (!set_timing(&desktop_mode))
		{
			log_error("WLROOTS: <%d> (delete_mode) [ERROR] could not restore desktop mode\n", m_id);
			return false;
		}
	}

	// Find and remove the entry from our custom list.
	bool found = false;
	for (auto it = m_custom_modes.begin(); it != m_custom_modes.end(); ++it)
	{
		if (it->ml.hactive == mode->hactive && it->ml.vactive == mode->vactive &&
		    it->ml.pclock  == mode->pclock  && it->ml.htotal == mode->htotal &&
		    it->ml.vtotal  == mode->vtotal)
		{
			m_custom_modes.erase(it);
			found = true;
			break;
		}
	}
	if (!found)
	{
		// Fall back to width/height/refresh match.
		int32_t want_mhz = (int32_t)(mode->vfreq * 1000.0);
		for (auto it = m_custom_modes.begin(); it != m_custom_modes.end(); ++it)
		{
			if (it->ml.width  == mode->width &&
			    it->ml.height == mode->height &&
			    (int32_t)(it->ml.vfreq * 1000.0) == want_mhz)
			{
				m_custom_modes.erase(it);
				found = true;
				break;
			}
		}
	}
	if (!found)
	{
		log_error("WLROOTS: <%d> (delete_mode) [WARNING] mode not in our custom list\n", m_id);
	}
	else
	{
		mode->platform_data = 0;
		log_verbose("WLROOTS: <%d> (delete_mode) mode removed\n", m_id);
	}
	return true;
}

// =========================================================================
//  update_mode
// =========================================================================

bool wlroots_timing::update_mode(modeline *mode)
{
	if (!mode) return false;
	if (!delete_mode(mode))
	{
		log_error("WLROOTS: <%d> (update_mode) [ERROR] delete failed\n", m_id);
		return false;
	}
	if (!add_mode(mode))
	{
		log_error("WLROOTS: <%d> (update_mode) [ERROR] add failed\n", m_id);
		return false;
	}
	return true;
}

// =========================================================================
//  set_timing
// =========================================================================

bool wlroots_timing::set_timing(modeline *mode)
{
    if (!mode) return false;
    if (!m_desktop_output)
    {
        log_error("WLROOTS: <%d> (set_timing) [ERROR] no screen detected\n", m_id);
        return false;
    }
    if (!m_managed)
    {
        log_error("WLROOTS: <%d> (set_timing) [WARNING] this screen is not managed by us\n", m_id);
        return false;
    }

    zwlr_output_mode_v1 *target = nullptr;
    int custom_w = 0, custom_h = 0, custom_refresh_mhz = 0;

    if (mode->type & MODE_DESKTOP)
    {
        // Restore the desktop mode = the mode that was current at init()
        // time. We snapshotted it into m_desktop_mode so that switching
        // to custom modes in between doesn't lose the original. This
        // mirrors the xrandr/KDE backends' m_desktop_mode handling.
        std::lock_guard<std::mutex> lock(m_mutex);
        target = m_desktop_mode;

        // Check if the desktop mode proxy is still in the compositor's
        // advertised list AND still has the same dimensions as the
        // desktop mode we captured at init. If the desktop mode was
        // itself a custom mode (e.g. set via sway's
        // `output <name> mode <w>x<h>@<r>` config), the compositor may
        // have either:
        //   (a) removed the proxy from the advertised list after we
        //       switched to a different custom mode via set_custom_mode,
        //       or
        //   (b) reused the same proxy object and updated its size/refresh
        //       events to reflect the new mode (sway does this).
        // In either case, set_mode(stale_proxy) would not restore the
        // original desktop mode — we need to use set_custom_mode(w, h,
        // refresh) with the cached desktop dimensions from init.
        if (target)
        {
            wlroots_mode_info *mi = find_mode_info_locked(this, target);
            if (!mi)
            {
                log_verbose("WLROOTS: <%d> (set_timing) desktop mode proxy %p no longer advertised, will use set_custom_mode\n",
                    m_id, (void *)target);
                target = nullptr;
            }
            else if (m_desktop_width && m_desktop_height &&
                (mi->width  != m_desktop_width ||
                 mi->height != m_desktop_height ||
                 mi->refresh_mhz != m_desktop_refresh_mhz))
            {
                log_verbose("WLROOTS: <%d> (set_timing) desktop mode proxy %p still advertised but dimensions changed (now %ux%u@%.3f, was %ux%u@%.3f), will use set_custom_mode\n",
                    m_id, (void *)target,
                    mi->width, mi->height, mi->refresh_mhz / 1000.0,
                    m_desktop_width, m_desktop_height,
                    m_desktop_refresh_mhz / 1000.0);
                target = nullptr;
            }
        }

        if (!target)
        {
            // The desktop mode proxy was destroyed (e.g. by the
            // compositor garbage-collecting it after a finished event)
            // or was a custom mode that's no longer advertised, or was
            // a custom mode whose proxy got reused for a different
            // resolution. Try to find a mode in the current advertised
            // list matching the cached desktop width/height/refresh.
            if (m_desktop_output && (m_desktop_width || m_desktop_height))
            {
                log_verbose("WLROOTS: <%d> (set_timing) desktop mode proxy gone, searching by %ux%u@%.3f\n",
                      m_id, m_desktop_width, m_desktop_height,
                      m_desktop_refresh_mhz / 1000.0);
                for (const auto &mi : m_desktop_output->modes)
                {
                    if (mi.width == m_desktop_width &&
                      mi.height == m_desktop_height &&
                      mi.refresh_mhz == m_desktop_refresh_mhz)
                    {
                        target = mi.proxy;
                        log_verbose("WLROOTS: <%d> (set_timing) found desktop mode by w/h/r: %p\n",
                              m_id, (void *)target);
                        break;
                    }
                }
            }
            if (!target)
            {
                // The desktop mode is not in the advertised list. If
                // we have cached dimensions, use set_custom_mode to
                // restore it. This handles the case where the desktop
                // mode was a custom mode that the compositor removed
                // or repurposed after we switched away from it.
                if (m_desktop_width && m_desktop_height)
                {
                    custom_w = m_desktop_width;
                    custom_h = m_desktop_height;
                    custom_refresh_mhz = m_desktop_refresh_mhz;
                    log_verbose("WLROOTS: <%d> (set_timing) restoring desktop via set_custom_mode %ux%u@%.3f\n",
                          m_id, custom_w, custom_h,
                          custom_refresh_mhz / 1000.0);
                    // target stays null — apply_configuration_for_mode
                    // will use set_custom_mode instead of set_mode.
                }
                else
                {
                    // Last resort: fall back to the live current mode
                    // (which is whatever we last switched to).
                    target = m_desktop_output ? m_desktop_output->current_mode : nullptr;
                }
            }
        }
        if (!target && !custom_w && !custom_h)
        {
            log_error("WLROOTS: <%d> (set_timing) [ERROR] no desktop mode to restore\n", m_id);
            return false;
        }
    }
    else
    {
        target = (zwlr_output_mode_v1 *)mode->platform_data;
        if (target)
        {
            // Verify the proxy is still advertised.
            if (!find_mode_by_proxy(target))
            {
                log_verbose("WLROOTS: <%d> (set_timing) proxy %p not in current list, searching by id=%d w=%d h=%d r=%d\n",
                    m_id, (void *)target, mode->id, mode->width, mode->height, mode->refresh);
                target = nullptr;   // fall through to search
            }
        }
        if (!target)
        {
            // platform_data == 0 means this is a custom mode that
            // was registered via add_mode(). Skip the advertised
            // mode list search entirely — custom modes are NOT
            // in the compositor's mode list. Go directly to
            // our custom mode cache.
            if (mode->platform_data != 0)
            {
                // The proxy was stale. Try to find an
                // advertised mode matching by id, then by
                // width/height/refresh.
                if (m_desktop_output)
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    for (const auto &mi : m_desktop_output->modes)
                    {
                        // Try id match first
                        if (mode->id != 0 && mi.id == mode->id)
                        {
                            target = mi.proxy;
                            log_verbose("WLROOTS: <%d> (set_timing) found mode by id=%d: %p\n",
                                m_id, mi.id, (void *)target);
                            break;
                        }
                        // Fall back to width/height/refresh
                        if (mi.width == (uint32_t)mode->width &&
                          mi.height == (uint32_t)mode->height &&
                          (int)(mi.refresh_mhz / 1000) == (int)mode->refresh)
                        {
                            target = mi.proxy;
                            log_verbose("WLROOTS: <%d> (set_timing) found mode by w/h/r: %p\n",
                                m_id, (void *)target);
                            break;
                        }
                    }
                }
            }
        }
        if (!target)
        {
            // Not in the advertised list - treat as a custom mode.
            // Look it up in our custom cache to get the exact
            // width/height/refresh (which may differ slightly from
            // what switchres passes in if the modeline was regenerated).
            custom_mode_entry *e = find_custom_mode_for(mode);
            if (e)
            {
                custom_w = e->ml.width;
                custom_h = e->ml.height;
                custom_refresh_mhz = (int)(e->ml.vfreq * 1000.0);
            }
            else
            {
                // Last resort: use the mode's own width/height/refresh.
                custom_w = mode->width;
                custom_h = mode->height;
                custom_refresh_mhz = (int)(mode->vfreq * 1000.0);
            }
            if (custom_w == 0 || custom_h == 0)
            {
                log_error("WLROOTS: <%d> (set_timing) [ERROR] custom mode has no resolution\n", m_id);
                return false;
            }
        }
    }

    log_verbose("WLROOTS: <%d> (set_timing) %s %s (%dx%d@%d mHz)\n",
        m_id, target ? "set_mode" : "set_custom_mode",
        target ? "" : "custom",
        target ? 0 : custom_w, target ? 0 : custom_h,
        target ? 0 : custom_refresh_mhz);

    bool ok = apply_configuration_for_mode(target,
                        custom_w, custom_h, custom_refresh_mhz,
                        m_desktop_output->x,
                        m_desktop_output->y,
                        m_desktop_output->transform);
    if (ok)
        log_verbose("WLROOTS: <%d> (set_timing) applied\n", m_id);
    return ok;
}

// =========================================================================
//  get_timing
//  Iterates the desktop output's advertised modes first, then our cached
//  custom modes (wlroots custom modes are ephemeral, not in the
//  compositor's list). For advertised modes we return only w/h/refresh
//  (the protocol doesn't expose CVT). For custom modes we return the
//  full modeline switchres generated.
// =========================================================================

bool wlroots_timing::get_timing(modeline *mode)
{
    if (!m_desktop_output)
    {
        log_error("WLROOTS: <%d> (get_timing) [ERROR] no screen detected\n", m_id);
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    int advertised_count = (int)m_desktop_output->modes.size();

    log_verbose("WLROOTS: <%d> (get_timing) position=%d, advertised=%d, custom=%zu\n",
        m_id, m_video_modes_position, advertised_count, m_custom_modes.size());

    if (m_video_modes_position < advertised_count)
    {
        // Advertised mode.
        const wlroots_mode_info &mi = m_desktop_output->modes[m_video_modes_position];
        memset(mode, 0, sizeof(*mode));
        modeline_from_mode_info(&mi, mode);

        // Tag the desktop mode. Proxy alone isn't enough — sway can
        // reuse the same proxy with different dimensions, and KDE's
        // set_custom_modes re-creates all proxies. Match by the cached
        // desktop w/h/refresh, which are stable across mode switches.
        // (wlroots doesn't expose CVT, so w/h/r is the most precise
        // comparison available.)
        bool proxy_and_dims_match = (m_desktop_mode == mi.proxy &&
            mi.width == m_desktop_width &&
            mi.height == m_desktop_height &&
            mi.refresh_mhz == m_desktop_refresh_mhz);

        bool dims_match = (m_desktop_width && m_desktop_height &&
            mi.width == m_desktop_width &&
            mi.height == m_desktop_height &&
            mi.refresh_mhz == m_desktop_refresh_mhz);

        if (proxy_and_dims_match || dims_match)
            mode->type |= MODE_DESKTOP;

        log_verbose("WLROOTS: <%d> (get_timing) returning advertised mode %p %ux%u@%.3f type=0x%x\n",
            m_id, (void *)mi.proxy, mi.width, mi.height,
            mi.refresh_mhz / 1000.0, mode->type);
        m_video_modes_position++;
    }
    else if ((m_video_modes_position - advertised_count) < (int)m_custom_modes.size())
    {
        // Custom mode from our cache.
        int idx = m_video_modes_position - advertised_count;
        const custom_mode_entry &e = m_custom_modes[idx];
        memset(mode, 0, sizeof(*mode));
        modeline_from_custom_entry(&e, mode);

        log_verbose("WLROOTS: <%d> (get_timing) returning custom mode %dx%d@%.3f type=0x%x\n",
            m_id, mode->width, mode->height, mode->vfreq, mode->type);
        m_video_modes_position++;
    }
    else
    {
        // List exhausted; reset the cursor (mirrors xrandr's behaviour).
        log_verbose("WLROOTS: <%d> (get_timing) list exhausted, resetting cursor\n", m_id);
        m_video_modes_position = 0;
    }
    return true;
}

// =========================================================================
//  process_modelist
// =========================================================================

bool wlroots_timing::process_modelist(std::vector<modeline *> modelist)
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
