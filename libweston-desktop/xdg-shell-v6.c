/*
 * Copyright © 2010-2012 Intel Corporation
 * Copyright © 2011-2012 Collabora, Ltd.
 * Copyright © 2013 Raspberry Pi Foundation
 * Copyright © 2016 Quentin "Sardem FF7" Glidic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "config.h"

#include <stdbool.h>
#include <assert.h>

#include <wayland-server.h>

#include "compositor.h"
#include "zalloc.h"
#include "xdg-shell-unstable-v6-server-protocol.h"
#include "xdg-shell-server-protocol.h"
#include "xdg-shell.h"

#include "libweston-desktop.h"
#include "internal.h"

#define WD_XDG_SHELL_PROTOCOL_VERSION 1

#define BIT_TEST(v,f) (((v) & (f))==(f))

/*
 * Implements the xdg_shell_unstable_v6 protocol. Most of the functionality is handled by the implementation of the new xdg_shell protocol.
 * The protocols are virtually identical so just enough code is needed here to translate the differences.
 * The POSITIONER and GRAVITY values are no longer bit flags. All the error codes are the same so are not translated.
 * The events are translated to the xdg_shell_unstable_v6 protocol events.
 */

static void
weston_desktop_xdg_positioner_v6_protocol_set_anchor(struct wl_client *wl_client,
						  struct wl_resource *resource,
						  enum zxdg_positioner_v6_anchor anchor)
{
	if (((anchor & ZXDG_POSITIONER_V6_ANCHOR_TOP ) &&
		(anchor & ZXDG_POSITIONER_V6_ANCHOR_BOTTOM)) ||
	   ((anchor & ZXDG_POSITIONER_V6_ANCHOR_LEFT) &&
		(anchor & ZXDG_POSITIONER_V6_ANCHOR_RIGHT))) {
		wl_resource_post_error(resource,
				       ZXDG_POSITIONER_V6_ERROR_INVALID_INPUT,
				       "same-axis values are not allowed");
		return;
	}

	enum xdg_positioner_anchor tr_anchor = 0;

	// Translate V6 bit flags to values in the new protocol.
	//
	if (BIT_TEST(anchor,ZXDG_POSITIONER_V6_ANCHOR_TOP|ZXDG_POSITIONER_V6_ANCHOR_LEFT)) {
		tr_anchor = XDG_POSITIONER_ANCHOR_TOP_LEFT;
	} else if (BIT_TEST(anchor,ZXDG_POSITIONER_V6_ANCHOR_TOP|ZXDG_POSITIONER_V6_ANCHOR_RIGHT)) {
		tr_anchor = XDG_POSITIONER_ANCHOR_TOP_RIGHT;
	} else if (BIT_TEST(anchor,ZXDG_POSITIONER_V6_ANCHOR_BOTTOM|ZXDG_POSITIONER_V6_ANCHOR_LEFT)) {
		tr_anchor = XDG_POSITIONER_ANCHOR_BOTTOM_LEFT;
	} else if (BIT_TEST(anchor,ZXDG_POSITIONER_V6_ANCHOR_BOTTOM|ZXDG_POSITIONER_V6_ANCHOR_RIGHT)) {
		tr_anchor = XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT;
	} else if (BIT_TEST(anchor,ZXDG_POSITIONER_V6_ANCHOR_TOP)) {
		tr_anchor = XDG_POSITIONER_ANCHOR_TOP;
	} else if (BIT_TEST(anchor,ZXDG_POSITIONER_V6_ANCHOR_BOTTOM)) {
		tr_anchor = XDG_POSITIONER_ANCHOR_BOTTOM;
	} else if (BIT_TEST(anchor,ZXDG_POSITIONER_V6_ANCHOR_LEFT)) {
		tr_anchor = XDG_POSITIONER_ANCHOR_LEFT;
	} else if (BIT_TEST(anchor,ZXDG_POSITIONER_V6_ANCHOR_RIGHT)) {
		tr_anchor = XDG_POSITIONER_ANCHOR_RIGHT;
	}

	weston_desktop_xdg_wm_positioner_protocol_set_anchor(wl_client, resource, tr_anchor);
}

static void
weston_desktop_xdg_positioner_v6_protocol_set_gravity(struct wl_client *wl_client,
						   struct wl_resource *resource,
						   enum zxdg_positioner_v6_gravity gravity)
{
	if (((gravity & ZXDG_POSITIONER_V6_GRAVITY_TOP) &&
	     (gravity & ZXDG_POSITIONER_V6_GRAVITY_BOTTOM)) ||
	    ((gravity & ZXDG_POSITIONER_V6_GRAVITY_LEFT) &&
	     (gravity & ZXDG_POSITIONER_V6_GRAVITY_RIGHT))) {
		wl_resource_post_error(resource,
				       ZXDG_POSITIONER_V6_ERROR_INVALID_INPUT,
				       "same-axis values are not allowed");
		return;
	}

	enum xdg_positioner_gravity tr_gravity = 0;

	// Translate V6 bit flags to values in the new protocol.
	//
	if (BIT_TEST(gravity,ZXDG_POSITIONER_V6_GRAVITY_TOP|ZXDG_POSITIONER_V6_GRAVITY_LEFT)) {
		tr_gravity = XDG_POSITIONER_GRAVITY_TOP_LEFT;
	} else if (BIT_TEST(gravity,ZXDG_POSITIONER_V6_GRAVITY_TOP|ZXDG_POSITIONER_V6_GRAVITY_RIGHT)) {
		tr_gravity = XDG_POSITIONER_GRAVITY_TOP_RIGHT;
	} else if (BIT_TEST(gravity,ZXDG_POSITIONER_V6_GRAVITY_BOTTOM|ZXDG_POSITIONER_V6_GRAVITY_LEFT)) {
		tr_gravity = XDG_POSITIONER_GRAVITY_BOTTOM_LEFT;
	} else if (BIT_TEST(gravity,ZXDG_POSITIONER_V6_GRAVITY_BOTTOM|ZXDG_POSITIONER_V6_GRAVITY_RIGHT)) {
		tr_gravity = XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT;
	} else if (BIT_TEST(gravity,ZXDG_POSITIONER_V6_GRAVITY_TOP)) {
		tr_gravity = XDG_POSITIONER_GRAVITY_TOP;
	} else if (BIT_TEST(gravity,ZXDG_POSITIONER_V6_GRAVITY_BOTTOM)) {
		tr_gravity = XDG_POSITIONER_GRAVITY_BOTTOM;
	} else if (BIT_TEST(gravity,ZXDG_POSITIONER_V6_GRAVITY_LEFT)) {
		tr_gravity = XDG_POSITIONER_GRAVITY_LEFT;
	} else if (BIT_TEST(gravity,ZXDG_POSITIONER_V6_GRAVITY_RIGHT)) {
		tr_gravity = XDG_POSITIONER_GRAVITY_RIGHT;
	}

	weston_desktop_xdg_wm_positioner_protocol_set_gravity(wl_client, resource, tr_gravity);
}

static void
weston_desktop_xdg_positioner_protocol_set_constraint_adjustment(struct wl_client *wl_client,
								 struct wl_resource *resource,
								 enum xdg_positioner_constraint_adjustment adjustment)
{
	// One to one mapping.
	//
	enum xdg_positioner_constraint_adjustment tr_adjustment = (int) adjustment;

	weston_desktop_xdg_wm_positioner_protocol_set_constraint_adjustment(wl_client, resource, tr_adjustment);
}

static void
weston_desktop_xdg_toplevel_protocol_resize(struct wl_client *wl_client,
					    struct wl_resource *resource,
					    struct wl_resource *seat_resource,
					    uint32_t serial,
					    enum zxdg_toplevel_v6_resize_edge edges)
{
	// One to one mapping between zxdg_toplevel_v6_resize_edge and xdg_toplevel_resize_edge values.
	//
	enum xdg_toplevel_resize_edge tr_edges = (int) edges;

	weston_desktop_xdg_wm_toplevel_protocol_resize(wl_client, resource, seat_resource, serial, tr_edges);
}


static const struct zxdg_positioner_v6_interface weston_desktop_xdg_positioner_implementation = {
	.destroy                   = weston_desktop_destroy_request,
	.set_size                  = weston_desktop_xdg_wm_positioner_protocol_set_size,
	.set_anchor_rect           = weston_desktop_xdg_wm_positioner_protocol_set_anchor_rect,
	.set_anchor                = weston_desktop_xdg_positioner_v6_protocol_set_anchor,
	.set_gravity               = weston_desktop_xdg_positioner_v6_protocol_set_gravity,
	.set_constraint_adjustment = weston_desktop_xdg_positioner_protocol_set_constraint_adjustment,
	.set_offset                = weston_desktop_xdg_wm_positioner_protocol_set_offset,
};

static const struct zxdg_toplevel_v6_interface weston_desktop_xdg_toplevel_implementation = {
	.destroy             = weston_desktop_destroy_request,
	.set_parent          = weston_desktop_xdg_wm_toplevel_protocol_set_parent,
	.set_title           = weston_desktop_xdg_wm_toplevel_protocol_set_title,
	.set_app_id          = weston_desktop_xdg_wm_toplevel_protocol_set_app_id,
	.show_window_menu    = weston_desktop_xdg_wm_toplevel_protocol_show_window_menu,
	.move                = weston_desktop_xdg_wm_toplevel_protocol_move,
	.resize              = weston_desktop_xdg_toplevel_protocol_resize,
	.set_min_size        = weston_desktop_xdg_wm_toplevel_protocol_set_min_size,
	.set_max_size        = weston_desktop_xdg_wm_toplevel_protocol_set_max_size,
	.set_maximized       = weston_desktop_xdg_wm_toplevel_protocol_set_maximized,
	.unset_maximized     = weston_desktop_xdg_wm_toplevel_protocol_unset_maximized,
	.set_fullscreen      = weston_desktop_xdg_wm_toplevel_protocol_set_fullscreen,
	.unset_fullscreen    = weston_desktop_xdg_wm_toplevel_protocol_unset_fullscreen,
	.set_minimized       = weston_desktop_xdg_wm_toplevel_protocol_set_minimized,
};


static void
weston_desktop_xdg_post_popup_configure(struct weston_desktop_xdg_popup *popup)
{
	zxdg_popup_v6_send_configure(popup->resource,
				     popup->geometry.x,
				     popup->geometry.y,
				     popup->geometry.width,
				     popup->geometry.height);
}

static void
weston_desktop_xdg_post_toplevel_configure(struct weston_desktop_xdg_toplevel *toplevel, struct wl_array *states)
{
	// states contains xdg_toplevel_state values. These are the same values as zxdg_toplevel_v6_state values
	//
	zxdg_toplevel_v6_send_configure(toplevel->resource,
					toplevel->pending.size.width,
					toplevel->pending.size.height,
					states);
}

static void
weston_desktop_xdg_post_toplevel_close(struct weston_desktop_xdg_toplevel *toplevel)
{
	zxdg_toplevel_v6_send_close(toplevel->resource);
}


static void
weston_desktop_xdg_post_popup_close(struct weston_desktop_xdg_popup *popup)
{
	zxdg_popup_v6_send_popup_done(popup->resource);
}


static void
weston_desktop_xdg_post_ping(struct wl_resource *resource, uint32_t serial)
{
	zxdg_shell_v6_send_ping(resource, serial);
}

static const struct zxdg_popup_v6_interface weston_desktop_xdg_popup_implementation = {
	.destroy             = weston_desktop_destroy_request,
	.grab                = weston_desktop_xdg_wm_popup_protocol_grab,
};


static const struct zxdg_surface_v6_interface weston_desktop_xdg_surface_implementation = {
	.destroy             = weston_desktop_destroy_request,
	.get_toplevel        = weston_desktop_xdg_wm_surface_protocol_get_toplevel,
	.get_popup           = weston_desktop_xdg_wm_surface_protocol_get_popup,
	.set_window_geometry = weston_desktop_xdg_wm_surface_protocol_set_window_geometry,
	.ack_configure       = weston_desktop_xdg_wm_surface_protocol_ack_configure,
};

static const struct weston_desktop_surface_implementation weston_desktop_xdg_surface_internal_implementation = {
	/* These are used for toplevel only */
	.set_maximized = weston_desktop_xdg_wm_toplevel_set_maximized,
	.set_fullscreen = weston_desktop_xdg_wm_toplevel_set_fullscreen,
	.set_resizing = weston_desktop_xdg_wm_toplevel_set_resizing,
	.set_activated = weston_desktop_xdg_wm_toplevel_set_activated,
	.set_size = weston_desktop_xdg_wm_toplevel_set_size,

	.get_maximized = weston_desktop_xdg_wm_toplevel_get_maximized,
	.get_fullscreen = weston_desktop_xdg_wm_toplevel_get_fullscreen,
	.get_resizing = weston_desktop_xdg_wm_toplevel_get_resizing,
	.get_activated = weston_desktop_xdg_wm_toplevel_get_activated,

	/* These are used for popup only */
	.update_position = weston_desktop_xdg_wm_popup_update_position,

	/* Common API */
	.committed = weston_desktop_xdg_wm_surface_committed,
	.ping = weston_desktop_xdg_wm_surface_ping,
	.close = weston_desktop_xdg_wm_surface_close,

	.destroy = weston_desktop_xdg_wm_surface_destroy,
};

// Declare the specific handlers & interfaces for the xdg-v6-shell protocol.
//
static const struct weston_desktop_xdg_handlers desktop_xdg_handlers = {
	.post_popup_configure = weston_desktop_xdg_post_popup_configure,
	.post_toplevel_configure = weston_desktop_xdg_post_toplevel_configure,
	.post_toplevel_close = weston_desktop_xdg_post_toplevel_close,
	.post_popup_close = weston_desktop_xdg_post_popup_close,
	.post_ping = weston_desktop_xdg_post_ping,
	.toplevel_iface = &zxdg_toplevel_v6_interface,
	.toplevel_impl = &weston_desktop_xdg_toplevel_implementation,
	.popup_iface = &zxdg_popup_v6_interface,
	.popup_impl = &weston_desktop_xdg_popup_implementation,
	.surface_iface = &zxdg_surface_v6_interface,
	.surface_impl = &weston_desktop_xdg_surface_implementation,
	.desktop_surface_impl = &weston_desktop_xdg_surface_internal_implementation,
	.positioner_iface = &zxdg_positioner_v6_interface,
	.positioner_impl = &weston_desktop_xdg_positioner_implementation,

};


static void
weston_desktop_xdg_shell_protocol_get_xdg_surface(struct wl_client *wl_client,
						  struct wl_resource *resource,
						  uint32_t id, struct wl_resource *surface_resource)
{
	weston_desktop_xdg_wm_shell_create_surface_(wl_client, resource, id, surface_resource, &desktop_xdg_handlers);
}

static void
weston_desktop_xdg_shell_protocol_create_positioner(struct wl_client *wl_client,
						    struct wl_resource *resource,
						    uint32_t id)
{
	weston_desktop_xdg_wm_shell_protocol_create_positioner_(wl_client, resource, id, &desktop_xdg_handlers);
}

static const struct zxdg_shell_v6_interface weston_desktop_xdg_shell_implementation = {
	.destroy = weston_desktop_destroy_request,
	.create_positioner = weston_desktop_xdg_shell_protocol_create_positioner,
	.get_xdg_surface = weston_desktop_xdg_shell_protocol_get_xdg_surface,
	.pong = weston_desktop_xdg_wm_shell_protocol_pong,
};

static void
weston_desktop_xdg_shell_bind(struct wl_client *client, void *data,
			      uint32_t version, uint32_t id)
{
	struct weston_desktop *desktop = data;

	weston_desktop_client_create(desktop, client, NULL,
				     &zxdg_shell_v6_interface,
				     &weston_desktop_xdg_shell_implementation,
				     version, id);
}

struct wl_global *
weston_desktop_xdg_shell_v6_create(struct weston_desktop *desktop, struct wl_display *display)
{
	return wl_global_create(display, &zxdg_shell_v6_interface,
				WD_XDG_SHELL_PROTOCOL_VERSION, desktop,
				weston_desktop_xdg_shell_bind);
}
