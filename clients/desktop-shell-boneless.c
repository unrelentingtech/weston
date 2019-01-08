/*
 * Copyright © 2011 Kristian Høgsberg
 * Copyright © 2011 Collabora, Ltd.
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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <cairo.h>
#include <sys/wait.h>
#include <linux/input.h>
#include <libgen.h>
#include <ctype.h>
#include <time.h>
#include <assert.h>
#ifdef __FreeBSD__
#include <signal.h>
#endif

#include <wayland-client.h>
#include "shared/config-parser.h"
#include "window.h"

#include "weston-desktop-shell-client-protocol.h"

struct desktop {
	struct display *display;
	struct weston_desktop_shell *shell;
	struct window *grab_window;
	struct widget *grab_widget;

	struct weston_config *config;

	enum cursor_type grab_cursor;
};

static void
desktop_shell_configure(void *data,
			struct weston_desktop_shell *desktop_shell,
			uint32_t edges,
			struct wl_surface *surface,
			int32_t width, int32_t height)
{
}

static void
desktop_shell_prepare_lock_surface(void *data,
				   struct weston_desktop_shell *desktop_shell)
{
	struct desktop *desktop = data;

	weston_desktop_shell_unlock(desktop->shell);
}

static void
desktop_shell_grab_cursor(void *data,
			  struct weston_desktop_shell *desktop_shell,
			  uint32_t cursor)
{
	struct desktop *desktop = data;

	switch (cursor) {
	case WESTON_DESKTOP_SHELL_CURSOR_NONE:
		desktop->grab_cursor = CURSOR_BLANK;
		break;
	case WESTON_DESKTOP_SHELL_CURSOR_BUSY:
		desktop->grab_cursor = CURSOR_WATCH;
		break;
	case WESTON_DESKTOP_SHELL_CURSOR_MOVE:
		desktop->grab_cursor = CURSOR_DRAGGING;
		break;
	case WESTON_DESKTOP_SHELL_CURSOR_RESIZE_TOP:
		desktop->grab_cursor = CURSOR_TOP;
		break;
	case WESTON_DESKTOP_SHELL_CURSOR_RESIZE_BOTTOM:
		desktop->grab_cursor = CURSOR_BOTTOM;
		break;
	case WESTON_DESKTOP_SHELL_CURSOR_RESIZE_LEFT:
		desktop->grab_cursor = CURSOR_LEFT;
		break;
	case WESTON_DESKTOP_SHELL_CURSOR_RESIZE_RIGHT:
		desktop->grab_cursor = CURSOR_RIGHT;
		break;
	case WESTON_DESKTOP_SHELL_CURSOR_RESIZE_TOP_LEFT:
		desktop->grab_cursor = CURSOR_TOP_LEFT;
		break;
	case WESTON_DESKTOP_SHELL_CURSOR_RESIZE_TOP_RIGHT:
		desktop->grab_cursor = CURSOR_TOP_RIGHT;
		break;
	case WESTON_DESKTOP_SHELL_CURSOR_RESIZE_BOTTOM_LEFT:
		desktop->grab_cursor = CURSOR_BOTTOM_LEFT;
		break;
	case WESTON_DESKTOP_SHELL_CURSOR_RESIZE_BOTTOM_RIGHT:
		desktop->grab_cursor = CURSOR_BOTTOM_RIGHT;
		break;
	case WESTON_DESKTOP_SHELL_CURSOR_ARROW:
	default:
		desktop->grab_cursor = CURSOR_LEFT_PTR;
	}
}

static const struct weston_desktop_shell_listener listener = {
	desktop_shell_configure,
	desktop_shell_prepare_lock_surface,
	desktop_shell_grab_cursor
};

static int
grab_surface_enter_handler(struct widget *widget, struct input *input,
			   float x, float y, void *data)
{
	struct desktop *desktop = data;

	return desktop->grab_cursor;
}

static void
grab_surface_destroy(struct desktop *desktop)
{
	widget_destroy(desktop->grab_widget);
	window_destroy(desktop->grab_window);
}

static void
grab_surface_create(struct desktop *desktop)
{
	struct wl_surface *s;

	desktop->grab_window = window_create_custom(desktop->display);
	window_set_user_data(desktop->grab_window, desktop);

	s = window_get_wl_surface(desktop->grab_window);
	weston_desktop_shell_set_grab_surface(desktop->shell, s);

	desktop->grab_widget =
		window_add_widget(desktop->grab_window, desktop);
	/* We set the allocation to 1x1 at 0,0 so the fake enter event
	 * at 0,0 will go to this widget. */
	widget_set_allocation(desktop->grab_widget, 0, 0, 1, 1);

	widget_set_enter_handler(desktop->grab_widget,
				 grab_surface_enter_handler);
}


static void
global_handler(struct display *display, uint32_t id,
	       const char *interface, uint32_t version, void *data)
{
	struct desktop *desktop = data;

	if (!strcmp(interface, "weston_desktop_shell")) {
		desktop->shell = display_bind(desktop->display,
					      id,
					      &weston_desktop_shell_interface,
					      1);
		weston_desktop_shell_add_listener(desktop->shell,
						  &listener,
						  desktop);
	}
}

static void
global_handler_remove(struct display *display, uint32_t id,
	       const char *interface, uint32_t version, void *data)
{
}

static void
sigchild_handler(int s)
{
	int status;
	pid_t pid;

	while (pid = waitpid(-1, &status, WNOHANG), pid > 0)
		fprintf(stderr, "child %d exited\n", pid);
}


int main(int argc, char *argv[])
{
	struct desktop desktop = { 0 };
	struct weston_config_section *s;
	const char *config_file;
	char *real_client;
	pid_t pid;

	config_file = weston_config_get_name_from_env();
	desktop.config = weston_config_parse(config_file);
	s = weston_config_get_section(desktop.config, "shell", NULL, NULL);
	weston_config_section_get_string(s, "real-client", &real_client, "");

	desktop.display = display_create(&argc, argv);
	if (desktop.display == NULL) {
		fprintf(stderr, "failed to create display: %m\n");
		return -1;
	}

	pid = fork();
	if (pid < 0) {
		fprintf(stderr, "fork failed: %m\n");
		exit(1);
	}

	if (!pid) {
		if (execl(real_client, real_client) < 0) {
			fprintf(stderr, "execl '%s' failed: %m\n", real_client);
			exit(1);
		}
	}

	display_set_user_data(desktop.display, &desktop);
	display_set_global_handler(desktop.display, global_handler);
	display_set_global_handler_remove(desktop.display, global_handler_remove);

	grab_surface_create(&desktop);

	signal(SIGCHLD, sigchild_handler);

	weston_desktop_shell_desktop_ready(desktop.shell);

	display_run(desktop.display);

	/* Cleanup */
	grab_surface_destroy(&desktop);
	weston_desktop_shell_destroy(desktop.shell);
	display_destroy(desktop.display);

	return 0;
}
