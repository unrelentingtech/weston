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

#ifndef LIBWESTON_DESKTOP_XDG_SHELL_H_
#define LIBWESTON_DESKTOP_XDG_SHELL_H_

enum weston_desktop_xdg_surface_role {
	WESTON_DESKTOP_XDG_SURFACE_ROLE_NONE,
	WESTON_DESKTOP_XDG_SURFACE_ROLE_TOPLEVEL,
	WESTON_DESKTOP_XDG_SURFACE_ROLE_POPUP,
};

struct weston_desktop_xdg_surface {
	struct wl_resource *resource;
	struct weston_desktop *desktop;
	struct weston_surface *surface;
	struct weston_desktop_surface *desktop_surface;
	bool configured;
	struct wl_event_source *configure_idle;
	struct wl_list configure_list; /* weston_desktop_xdg_surface_configure::link */
	const struct weston_desktop_xdg_handlers* handlers;

	bool has_next_geometry;
	struct weston_geometry next_geometry;

	enum weston_desktop_xdg_surface_role role;
};

struct weston_desktop_xdg_surface_configure {
	struct wl_list link; /* weston_desktop_xdg_surface::configure_list */
	uint32_t serial;
};

struct weston_desktop_xdg_toplevel_state {
	bool maximized;
	bool fullscreen;
	bool resizing;
	bool activated;
};

struct weston_desktop_xdg_toplevel_configure {
	struct weston_desktop_xdg_surface_configure base;
	struct weston_desktop_xdg_toplevel_state state;
	struct weston_size size;
};
#include "xdg-shell-server-protocol.h"

struct weston_desktop_xdg_toplevel {
	struct weston_desktop_xdg_surface base;

	struct wl_resource *resource;
	bool added;
	struct {
		struct weston_desktop_xdg_toplevel_state state;
		struct weston_size size;
	} pending;
	struct {
		struct weston_desktop_xdg_toplevel_state state;
		struct weston_size size;
		struct weston_size min_size, max_size;
	} next;
	struct {
		struct weston_desktop_xdg_toplevel_state state;
		struct weston_size min_size, max_size;
	} current;
};

struct weston_desktop_xdg_popup {
	struct weston_desktop_xdg_surface base;

	struct wl_resource *resource;
	bool committed;
	struct weston_desktop_xdg_surface *parent;
	struct weston_desktop_seat *seat;
	struct weston_geometry geometry;
};

struct weston_desktop_xdg_handlers {
	void (*post_popup_configure)(struct weston_desktop_xdg_popup *popup);
	void (*post_toplevel_configure)(struct weston_desktop_xdg_toplevel *surface, struct wl_array *states);
	void (*post_toplevel_close)(struct weston_desktop_xdg_toplevel *surface);
	void (*post_popup_close)(struct weston_desktop_xdg_popup *popup);
	void (*post_ping)(struct wl_resource *resource, uint32_t serial);

	const void *desktop_surface_impl;
	const struct wl_interface *toplevel_iface;
	const void *toplevel_impl;
	const struct wl_interface *popup_iface;
	const void *popup_impl;
	const struct wl_interface *surface_iface;
	const void *surface_impl;
	const struct wl_interface *positioner_iface;
	const void *positioner_impl;
};


#define weston_desktop_surface_role_biggest_size (sizeof(struct weston_desktop_xdg_toplevel))
#define weston_desktop_surface_configure_biggest_size (sizeof(struct weston_desktop_xdg_toplevel))


void
weston_desktop_xdg_wm_positioner_protocol_set_size(struct wl_client *wl_client,
		struct wl_resource *resource,
		int32_t width, int32_t height);

void
weston_desktop_xdg_wm_positioner_protocol_set_anchor_rect(struct wl_client *wl_client,
		struct wl_resource *resource,
		int32_t x, int32_t y,
		int32_t width, int32_t height);

void
weston_desktop_xdg_wm_positioner_protocol_set_anchor(struct wl_client *wl_client,
		struct wl_resource *resource,
		enum xdg_positioner_anchor anchor);

void
weston_desktop_xdg_wm_positioner_protocol_set_gravity(struct wl_client *wl_client,
		struct wl_resource *resource,
		enum xdg_positioner_gravity gravity);

void
weston_desktop_xdg_wm_positioner_protocol_set_constraint_adjustment(struct wl_client *wl_client,
		struct wl_resource *resource,
		enum xdg_positioner_constraint_adjustment constraint_adjustment);

void
weston_desktop_xdg_wm_positioner_protocol_set_offset(struct wl_client *wl_client,
		struct wl_resource *resource,
		int32_t x, int32_t y);

void
weston_desktop_xdg_wm_toplevel_protocol_set_parent(struct wl_client *wl_client,
		struct wl_resource *resource,
		struct wl_resource *parent_resource);

void
weston_desktop_xdg_wm_toplevel_protocol_set_title(struct wl_client *wl_client,
		struct wl_resource *resource,
		const char *title);

void
weston_desktop_xdg_wm_toplevel_protocol_set_app_id(struct wl_client *wl_client,
		struct wl_resource *resource,
		const char *app_id);

void
weston_desktop_xdg_wm_toplevel_protocol_show_window_menu(struct wl_client *wl_client,
		struct wl_resource *resource,
		struct wl_resource *seat_resource,
		uint32_t serial,
		int32_t x, int32_t y);

void
weston_desktop_xdg_wm_toplevel_protocol_move(struct wl_client *wl_client,
		struct wl_resource *resource,
		struct wl_resource *seat_resource,
		uint32_t serial);

void
weston_desktop_xdg_wm_toplevel_protocol_resize(struct wl_client *wl_client,
		struct wl_resource *resource,
		struct wl_resource *seat_resource,
		uint32_t serial,
		enum xdg_toplevel_resize_edge edges);

void
weston_desktop_xdg_wm_toplevel_protocol_set_min_size(struct wl_client *wl_client,
		struct wl_resource *resource,
		int32_t width, int32_t height);

void
weston_desktop_xdg_wm_toplevel_protocol_set_max_size(struct wl_client *wl_client,
		struct wl_resource *resource,
		int32_t width, int32_t height);

void
weston_desktop_xdg_wm_toplevel_protocol_set_maximized(struct wl_client *wl_client, struct wl_resource *resource);

void
weston_desktop_xdg_wm_toplevel_protocol_unset_maximized(struct wl_client *wl_client, struct wl_resource *resource);

void
weston_desktop_xdg_wm_toplevel_protocol_set_fullscreen(struct wl_client *wl_client,
		struct wl_resource *resource,
		struct wl_resource *output_resource);

void
weston_desktop_xdg_wm_toplevel_protocol_unset_fullscreen(struct wl_client *wl_client, struct wl_resource *resource);

void
weston_desktop_xdg_wm_toplevel_protocol_set_minimized(struct wl_client *wl_client, struct wl_resource *resource);

void
weston_desktop_xdg_wm_surface_protocol_get_toplevel(struct wl_client *wl_client,
		struct wl_resource *resource,
		uint32_t id);

void
weston_desktop_xdg_wm_surface_protocol_get_popup(struct wl_client *wl_client,
		struct wl_resource *resource,
		uint32_t id,
		struct wl_resource *parent_resource,
		struct wl_resource *positioner_resource);

bool
weston_desktop_xdg_surface_check_role(struct weston_desktop_xdg_surface *surface);

void
weston_desktop_xdg_wm_surface_protocol_set_window_geometry(struct wl_client *wl_client,
		struct wl_resource *resource,
		int32_t x, int32_t y,
		int32_t width, int32_t height);

void
weston_desktop_xdg_wm_surface_protocol_ack_configure(struct wl_client *wl_client,
		struct wl_resource *resource,
		uint32_t serial);

void
weston_desktop_xdg_wm_surface_ping(struct weston_desktop_surface *dsurface,
		uint32_t serial, void *user_data);

void
weston_desktop_xdg_wm_popup_protocol_grab(struct wl_client *wl_client,
		struct wl_resource *resource,
		struct wl_resource *seat_resource,
		uint32_t serial);

void
weston_desktop_xdg_wm_shell_protocol_create_positioner_(struct wl_client *wl_client,
		struct wl_resource *resource,
		uint32_t id,
		const struct weston_desktop_xdg_handlers *handlers );

void
weston_desktop_xdg_wm_shell_create_surface_(struct wl_client *wl_client,
		struct wl_resource *resource,
		uint32_t id,
		struct wl_resource *surface_resource,
		const struct weston_desktop_xdg_handlers *handlers );

void
weston_desktop_xdg_wm_shell_protocol_pong(struct wl_client *wl_client,
		struct wl_resource *resource,
		uint32_t serial);

void
weston_desktop_xdg_wm_toplevel_set_maximized(struct weston_desktop_surface *dsurface,
		void *user_data, bool maximized);

void
weston_desktop_xdg_wm_toplevel_set_fullscreen(struct weston_desktop_surface *dsurface,
		void *user_data, bool fullscreen);

void
weston_desktop_xdg_wm_toplevel_set_resizing(struct weston_desktop_surface *dsurface,
		void *user_data, bool resizing);

void
weston_desktop_xdg_wm_toplevel_set_activated(struct weston_desktop_surface *dsurface,
		void *user_data, bool activated);

void
weston_desktop_xdg_wm_toplevel_set_size(struct weston_desktop_surface *dsurface,
		void *user_data, int32_t width, int32_t height);

void
weston_desktop_xdg_wm_toplevel_committed(struct weston_desktop_xdg_toplevel *toplevel,
		int32_t sx, int32_t sy);

void
weston_desktop_xdg_toplevel_close(struct weston_desktop_xdg_toplevel *toplevel);

bool
weston_desktop_xdg_wm_toplevel_get_maximized(struct weston_desktop_surface *dsurface,
		void *user_data);

bool
weston_desktop_xdg_wm_toplevel_get_fullscreen(struct weston_desktop_surface *dsurface,
		void *user_data);

bool
weston_desktop_xdg_wm_toplevel_get_resizing(struct weston_desktop_surface *dsurface,
		void *user_data);

bool
weston_desktop_xdg_wm_toplevel_get_activated(struct weston_desktop_surface *dsurface,
		void *user_data);

void
weston_desktop_xdg_wm_surface_committed(struct weston_desktop_surface *dsurface,
		void *user_data,
	    int32_t sx, int32_t sy);

void
weston_desktop_xdg_wm_surface_close(struct weston_desktop_surface *dsurface,
		void *user_data);

void
weston_desktop_xdg_wm_popup_update_position(struct weston_desktop_surface *dsurface, void *user_data);

void
weston_desktop_xdg_wm_surface_destroy(struct weston_desktop_surface *dsurface,
		void *user_data);

#endif /* LIBWESTON_DESKTOP_XDG_SHELL_H_ */
