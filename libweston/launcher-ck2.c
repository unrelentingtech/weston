/*
 * Copyright © 2013 David Herrmann
 * Copyright © 2019 Greg V
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ConsoleKit/libconsolekit.h>

#include "compositor.h"
#include "dbus.h"
#include "launcher-impl.h"

#define DRM_MAJOR 226

/* major()/minor() */
#ifdef MAJOR_IN_MKDEV
#include <sys/mkdev.h>
#endif
#ifdef MAJOR_IN_SYSMACROS
#include <sys/sysmacros.h>
#endif

struct launcher_ck2 {
	struct weston_launcher base;
	struct weston_compositor *compositor;
	bool sync_drm;
	char *seat;
	char *sid;
	unsigned int vtnr;
	int vt;
	int kb_mode;

	LibConsoleKit *ckit;
	DBusConnection *dbus;
	struct wl_event_source *dbus_ctx;
	DBusPendingCall *pending_active;
};

static int
launcher_ck2_take_device(struct launcher_ck2 *wl, uint32_t major,
				uint32_t minor, bool *paused_out)
{
	DBusMessage *m, *reply;
	bool b;
	int r, fd;
	dbus_bool_t paused;

	m = dbus_message_new_method_call("org.freedesktop.ConsoleKit",
					 wl->sid,
					 "org.freedesktop.ConsoleKit.Session",
					 "TakeDevice");
	if (!m)
		return -ENOMEM;

	b = dbus_message_append_args(m,
						 DBUS_TYPE_UINT32, &major,
						 DBUS_TYPE_UINT32, &minor,
						 DBUS_TYPE_INVALID);
	if (!b) {
		r = -ENOMEM;
		goto err_unref;
	}

	reply = dbus_connection_send_with_reply_and_block(wl->dbus, m,
								-1, NULL);
	if (!reply) {
		r = -ENODEV;
		goto err_unref;
	}

	b = dbus_message_get_args(reply, NULL,
					DBUS_TYPE_UNIX_FD, &fd,
					DBUS_TYPE_BOOLEAN, &paused,
					DBUS_TYPE_INVALID);
	if (!b) {
		r = -ENODEV;
		goto err_reply;
	}

	r = fd;
	if (paused_out)
		*paused_out = paused;

err_reply:
	dbus_message_unref(reply);
err_unref:
	dbus_message_unref(m);
	return r;
}

static void
launcher_ck2_release_device(struct launcher_ck2 *wl, uint32_t major,
					 uint32_t minor)
{
	DBusMessage *m;
	bool b;

	m = dbus_message_new_method_call("org.freedesktop.ConsoleKit",
					 wl->sid,
					 "org.freedesktop.ConsoleKit.Session",
					 "ReleaseDevice");
	if (m) {
		b = dbus_message_append_args(m,
							 DBUS_TYPE_UINT32, &major,
							 DBUS_TYPE_UINT32, &minor,
							 DBUS_TYPE_INVALID);
		if (b)
			dbus_connection_send(wl->dbus, m, NULL);
		dbus_message_unref(m);
	}
}

static void
launcher_ck2_pause_device_complete(struct launcher_ck2 *wl, uint32_t major,
						uint32_t minor)
{
	DBusMessage *m;
	bool b;

	m = dbus_message_new_method_call("org.freedesktop.ConsoleKit",
					 wl->sid,
					 "org.freedesktop.ConsoleKit.Session",
					 "PauseDeviceComplete");
	if (m) {
		b = dbus_message_append_args(m,
							 DBUS_TYPE_UINT32, &major,
							 DBUS_TYPE_UINT32, &minor,
							 DBUS_TYPE_INVALID);
		if (b)
			dbus_connection_send(wl->dbus, m, NULL);
		dbus_message_unref(m);
	}
}

static int
launcher_ck2_open(struct weston_launcher *launcher, const char *path, int flags)
{
	struct launcher_ck2 *wl = wl_container_of(launcher, wl, base);
	struct stat st;
	int fl, r, fd;

	r = stat(path, &st);
	if (r < 0)
		return -1;
	if (!S_ISCHR(st.st_mode)) {
		errno = ENODEV;
		return -1;
	}

	fd = launcher_ck2_take_device(wl, major(st.st_rdev),
							 minor(st.st_rdev), NULL);
	if (fd < 0)
		return fd;

	/* Compared to weston_launcher_open() we cannot specify the open-mode
	 * directly. Instead, ck2 passes us an fd with sane default modes.
	 * For DRM and evdev this means O_RDWR | O_CLOEXEC. If we want
	 * something else, we need to change it afterwards. We currently
	 * only support setting O_NONBLOCK. Changing access-modes is not
	 * possible so accept whatever ck2 passes us. */

	fl = fcntl(fd, F_GETFL);
	if (fl < 0) {
		r = -errno;
		goto err_close;
	}

	if (flags & O_NONBLOCK)
		fl |= O_NONBLOCK;

	r = fcntl(fd, F_SETFL, fl);
	if (r < 0) {
		r = -errno;
		goto err_close;
	}
	return fd;

err_close:
	close(fd);
	launcher_ck2_release_device(wl, major(st.st_rdev),
						 minor(st.st_rdev));
	errno = -r;
	return -1;
}

static void
launcher_ck2_close(struct weston_launcher *launcher, int fd)
{
	struct launcher_ck2 *wl = wl_container_of(launcher, wl, base);
	struct stat st;
	int r;

	r = fstat(fd, &st);
	close(fd);
	if (r < 0) {
		weston_log("ck2: cannot fstat fd %d: %m\n", fd);
		return;
	}

	if (!S_ISCHR(st.st_mode)) {
		weston_log("ck2: invalid device passed\n");
		return;
	}

release:
	launcher_ck2_release_device(wl, major(st.st_rdev),
						 minor(st.st_rdev));
}

static int
launcher_ck2_activate_vt(struct weston_launcher *launcher, int vt)
{
	struct launcher_ck2 *wl = wl_container_of(launcher, wl, base);
	DBusMessage *m;
	bool b;
	int r;

	m = dbus_message_new_method_call("org.freedesktop.ConsoleKit",
					 "/org/freedesktop/ConsoleKit/Seat1",
					 "org.freedesktop.ConsoleKit.Seat",
					 "SwitchTo");
	if (!m)
		return -ENOMEM;

	b = dbus_message_append_args(m,
						 DBUS_TYPE_UINT32, &vt,
						 DBUS_TYPE_INVALID);
	if (!b) {
		r = -ENOMEM;
		goto err_unref;
	}

	dbus_connection_send(wl->dbus, m, NULL);
	r = 0;

 err_unref:
	dbus_message_unref(m);
	return r;
}

static void
launcher_ck2_set_active(struct launcher_ck2 *wl, bool active)
{
	if (!wl->compositor->session_active == !active)
		return;

	wl->compositor->session_active = active;

	wl_signal_emit(&wl->compositor->session_signal,
					 wl->compositor);
}

static void
parse_active(struct launcher_ck2 *wl, DBusMessage *m, DBusMessageIter *iter)
{
	DBusMessageIter sub;
	dbus_bool_t b;

	if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_VARIANT)
		return;

	dbus_message_iter_recurse(iter, &sub);

	if (dbus_message_iter_get_arg_type(&sub) != DBUS_TYPE_BOOLEAN)
		return;

	dbus_message_iter_get_basic(&sub, &b);

	/* If the backend requested DRM master-device synchronization, we only
	 * wake-up the compositor once the master-device is up and running. For
	 * other backends, we immediately forward the Active-change event. */
	if (!wl->sync_drm || !b)
		launcher_ck2_set_active(wl, b);
}

static void
get_active_cb(DBusPendingCall *pending, void *data)
{
	struct launcher_ck2 *wl = data;
	DBusMessageIter iter;
	DBusMessage *m;
	int type;

	dbus_pending_call_unref(wl->pending_active);
	wl->pending_active = NULL;

	m = dbus_pending_call_steal_reply(pending);
	if (!m)
		return;

	type = dbus_message_get_type(m);
	if (type == DBUS_MESSAGE_TYPE_METHOD_RETURN &&
			dbus_message_iter_init(m, &iter))
		parse_active(wl, m, &iter);

	dbus_message_unref(m);
}

static void
launcher_ck2_get_active(struct launcher_ck2 *wl)
{
	DBusPendingCall *pending;
	DBusMessage *m;
	bool b;
	const char *iface, *name;

	m = dbus_message_new_method_call("org.freedesktop.ConsoleKit",
					 wl->sid,
					 "org.freedesktop.DBus.Properties",
					 "Get");
	if (!m)
		return;

	iface = "org.freedesktop.ConsoleKit.Session";
	name = "active";
	b = dbus_message_append_args(m,
						 DBUS_TYPE_STRING, &iface,
						 DBUS_TYPE_STRING, &name,
						 DBUS_TYPE_INVALID);
	if (!b)
		goto err_unref;

	b = dbus_connection_send_with_reply(wl->dbus, m, &pending, -1);
	if (!b)
		goto err_unref;

	b = dbus_pending_call_set_notify(pending, get_active_cb, wl, NULL);
	if (!b) {
		dbus_pending_call_cancel(pending);
		dbus_pending_call_unref(pending);
		goto err_unref;
	}

	if (wl->pending_active) {
		dbus_pending_call_cancel(wl->pending_active);
		dbus_pending_call_unref(wl->pending_active);
	}
	wl->pending_active = pending;
	return;

err_unref:
	dbus_message_unref(m);
}

static void
disconnected_dbus(struct launcher_ck2 *wl)
{
	weston_log("ck2: dbus connection lost, exiting..\n");
	exit(-1);
}

static void
session_removed(struct launcher_ck2 *wl, DBusMessage *m)
{
	const char *name, *obj;
	bool r;

	r = dbus_message_get_args(m, NULL,
					DBUS_TYPE_STRING, &name,
					DBUS_TYPE_OBJECT_PATH, &obj,
					DBUS_TYPE_INVALID);
	if (!r) {
		weston_log("ck2: cannot parse SessionRemoved dbus signal\n");
		return;
	}

	if (!strcmp(name, wl->sid)) {
		weston_log("ck2: our session got closed, exiting..\n");
		exit(-1);
	}
}

static void
property_changed(struct launcher_ck2 *wl, DBusMessage *m)
{
	DBusMessageIter iter, sub, entry;
	const char *interface, *name;

	if (!dbus_message_iter_init(m, &iter) ||
			dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING)
		goto error;

	dbus_message_iter_get_basic(&iter, &interface);

	if (!dbus_message_iter_next(&iter) ||
			dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY)
		goto error;

	dbus_message_iter_recurse(&iter, &sub);

	while (dbus_message_iter_get_arg_type(&sub) == DBUS_TYPE_DICT_ENTRY) {
		dbus_message_iter_recurse(&sub, &entry);

		if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_STRING)
			goto error;

		dbus_message_iter_get_basic(&entry, &name);
		if (!dbus_message_iter_next(&entry))
			goto error;

		if (!strcmp(name, "active")) {
			parse_active(wl, m, &entry);
			return;
		}

		dbus_message_iter_next(&sub);
	}

	if (!dbus_message_iter_next(&iter) ||
			dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY)
		goto error;

	dbus_message_iter_recurse(&iter, &sub);

	while (dbus_message_iter_get_arg_type(&sub) == DBUS_TYPE_STRING) {
		dbus_message_iter_get_basic(&sub, &name);

		if (!strcmp(name, "active")) {
			launcher_ck2_get_active(wl);
			return;
		}

		dbus_message_iter_next(&sub);
	}

	return;

error:
	weston_log("ck2: cannot parse PropertiesChanged dbus signal\n");
}

static void
device_paused(struct launcher_ck2 *wl, DBusMessage *m)
{
	bool r;
	const char *type;
	uint32_t major, minor;

	r = dbus_message_get_args(m, NULL,
					DBUS_TYPE_UINT32, &major,
					DBUS_TYPE_UINT32, &minor,
					DBUS_TYPE_STRING, &type,
					DBUS_TYPE_INVALID);
	if (!r) {
		weston_log("ck2: cannot parse PauseDevice dbus signal\n");
		return;
	}

	/* "pause" means synchronous pausing. Acknowledge it unconditionally
	 * as we support asynchronous device shutdowns, anyway.
	 * "force" means asynchronous pausing.
	 * "gone" means the device is gone. We handle it the same as "force" as
	 * a following udev event will be caught, too.
	 *
	 * If it's our main DRM device, tell the compositor to go asleep. */

	if (!strcmp(type, "pause"))
		launcher_ck2_pause_device_complete(wl, major, minor);

	if (wl->sync_drm /*&& major == DRM_MAJOR*/)
		launcher_ck2_set_active(wl, false);
}

static void
device_resumed(struct launcher_ck2 *wl, DBusMessage *m)
{
	bool r;
	uint32_t major;

	r = dbus_message_get_args(m, NULL,
					DBUS_TYPE_UINT32, &major,
					/*DBUS_TYPE_UINT32, &minor,
					DBUS_TYPE_UNIX_FD, &fd,*/
					DBUS_TYPE_INVALID);
	if (!r) {
		weston_log("ck2: cannot parse ResumeDevice dbus signal\n");
		return;
	}

	/* DeviceResumed messages provide us a new file-descriptor for
	 * resumed devices. For DRM devices it's the same as before, for evdev
	 * devices it's a new open-file. As we reopen evdev devices, anyway,
	 * there is no need for us to handle this event for evdev. For DRM, we
	 * notify the compositor to wake up. */

	if (wl->sync_drm /*&& major == DRM_MAJOR*/)
		launcher_ck2_set_active(wl, true);
}

static DBusHandlerResult
filter_dbus(DBusConnection *c, DBusMessage *m, void *data)
{
	struct launcher_ck2 *wl = data;

	if (dbus_message_is_signal(m, DBUS_INTERFACE_LOCAL, "Disconnected")) {
		disconnected_dbus(wl);
	} else if (dbus_message_is_signal(m, "org.freedesktop.ConsoleKit.Manager",
						"SessionRemoved")) {
		session_removed(wl, m);
	} else if (dbus_message_is_signal(m, "org.freedesktop.DBus.Properties",
						"PropertiesChanged")) {
		property_changed(wl, m);
	} else if (dbus_message_is_signal(m, "org.freedesktop.ConsoleKit.Session",
						"PauseDevice")) {
		device_paused(wl, m);
	} else if (dbus_message_is_signal(m, "org.freedesktop.ConsoleKit.Session",
						"ResumeDevice")) {
		device_resumed(wl, m);
	}

	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static int
launcher_ck2_setup_dbus(struct launcher_ck2 *wl)
{
	bool b;
	int r;

	b = dbus_connection_add_filter(wl->dbus, filter_dbus, wl, NULL);
	if (!b) {
		weston_log("ck2: cannot add dbus filter\n");
		r = -ENOMEM;
		goto err_sid;
	}

	r = weston_dbus_add_match_signal(wl->dbus,
					 "org.freedesktop.ConsoleKit",
					 "org.freedesktop.ConsoleKit.Manager",
					 "SessionRemoved",
					 "/org/freedesktop/ConsoleKit");
	if (r < 0) {
		weston_log("ck2: cannot add dbus match 1\n");
		goto err_sid;
	}

	r = weston_dbus_add_match_signal(wl->dbus,
					"org.freedesktop.ConsoleKit",
					"org.freedesktop.ConsoleKit.Session",
					"PauseDevice",
					wl->sid);
	if (r < 0) {
		weston_log("ck2: cannot add dbus match 2\n");
		goto err_sid;
	}

	r = weston_dbus_add_match_signal(wl->dbus,
					"org.freedesktop.ConsoleKit",
					"org.freedesktop.ConsoleKit.Session",
					"ResumeDevice",
					wl->sid);
	if (r < 0) {
		weston_log("ck2: cannot add dbus match 3\n");
		goto err_sid;
	}

	r = weston_dbus_add_match_signal(wl->dbus,
					"org.freedesktop.ConsoleKit",
					"org.freedesktop.DBus.Properties",
					"PropertiesChanged",
					wl->sid);
	if (r < 0) {
		weston_log("ck2: cannot add dbus match 4\n");
		goto err_sid;
	}

	return 0;

err_sid:
	/* don't remove any dbus-match as the connection is closed, anyway */
	return r;
}

static void
launcher_ck2_destroy_dbus(struct launcher_ck2 *wl)
{
	/* don't remove any dbus-match as the connection is closed, anyway */
}

static int
launcher_ck2_take_control(struct launcher_ck2 *wl)
{
	DBusError err;
	DBusMessage *m, *reply;
	dbus_bool_t force;
	bool b;
	int r;

	dbus_error_init(&err);

	m = dbus_message_new_method_call("org.freedesktop.ConsoleKit",
					 wl->sid,
					 "org.freedesktop.ConsoleKit.Session",
					 "TakeControl");
	if (!m)
		return -ENOMEM;

	force = false;
	b = dbus_message_append_args(m,
						 DBUS_TYPE_BOOLEAN, &force,
						 DBUS_TYPE_INVALID);
	if (!b) {
		r = -ENOMEM;
		goto err_unref;
	}

	reply = dbus_connection_send_with_reply_and_block(wl->dbus,
								m, -1, &err);
	if (!reply) {
		if (dbus_error_has_name(&err, DBUS_ERROR_UNKNOWN_METHOD))
			weston_log("ck2: old ConsoleKit2 version detected\n");
		else
			weston_log("ck2: cannot take control over session %s\n", wl->sid);

		dbus_error_free(&err);
		r = -EIO;
		goto err_unref;
	}

	dbus_message_unref(reply);
	dbus_message_unref(m);
	return 0;

err_unref:
	dbus_message_unref(m);
	return r;
}

static void
launcher_ck2_release_control(struct launcher_ck2 *wl)
{
	DBusMessage *m;

	m = dbus_message_new_method_call("org.freedesktop.ConsoleKit",
					 wl->sid,
					 "org.freedesktop.ConsoleKit.Session",
					 "ReleaseControl");
	if (m) {
		dbus_connection_send(wl->dbus, m, NULL);
		dbus_message_unref(m);
	}
}

static int
launcher_ck2_activate(struct launcher_ck2 *wl)
{
	DBusMessage *m;

	m = dbus_message_new_method_call("org.freedesktop.ConsoleKit",
					 wl->sid,
					 "org.freedesktop.ConsoleKit.Session",
					 "Activate");
	if (!m)
		return -ENOMEM;

	dbus_connection_send(wl->dbus, m, NULL);
	return 0;
}

static int
launcher_ck2_connect(struct weston_launcher **out, struct weston_compositor *compositor,
			int tty, const char *seat_id, bool sync_drm)
{
	struct launcher_ck2 *wl;
	struct wl_event_loop *loop;
	char *t;
	int r;
	GError *gerr;

	wl = zalloc(sizeof(*wl));
	if (wl == NULL) {
		r = -ENOMEM;
		goto err_out;
	}

	wl->base.iface = &launcher_ck2_iface;
	wl->compositor = compositor;
	wl->sync_drm = sync_drm;

	wl->seat = strdup(seat_id);
	if (!wl->seat) {
		r = -ENOMEM;
		goto err_wl;
	}

	wl->ckit = lib_consolekit_new();

	gerr = NULL;
	r = lib_consolekit_pid_get_session(wl->ckit, getpid(), &wl->sid, &gerr);
	if (gerr != NULL) {
		weston_log("ck2: not running in a ConsoleKit2 session: %s\n", gerr->message);
		goto err_seat;
	}

	t = NULL;
	gerr = NULL;
	r = lib_consolekit_session_get_seat(wl->ckit, wl->sid, &t, &gerr);
	if (gerr != NULL) {
		weston_log("ck2: failed to get session seat: %s\n", gerr->message);
		free(t);
		goto err_session;
	} else if (false /*strcmp(seat_id, t)*/) {
		weston_log("ck2: weston's seat '%s' differs from session-seat '%s'\n",
				 seat_id, t);
		r = -EINVAL;
		free(t);
		goto err_session;
	}

	r = 0 /*strcmp(t, "seat0")*/;
	free(t);
	if (r == 0) {
		gerr = NULL;
		r = lib_consolekit_session_get_vt(wl->ckit, wl->sid, &wl->vtnr, &gerr);
		if (gerr != NULL) {
			weston_log("ck2: session not running on a VT: %s\n", gerr->message);
			goto err_session;
		} else if (tty > 0 && wl->vtnr != (unsigned int )tty) {
			weston_log("ck2: requested VT --tty=%d differs from real session VT %u\n",
					 tty, wl->vtnr);
			r = -EINVAL;
			goto err_session;
		}
	}

	loop = wl_display_get_event_loop(compositor->wl_display);
	r = weston_dbus_open(loop, DBUS_BUS_SYSTEM, &wl->dbus, &wl->dbus_ctx);
	if (r < 0) {
		weston_log("ck2: cannot connect to system dbus\n");
		goto err_session;
	}

	r = launcher_ck2_setup_dbus(wl);
	if (r < 0)
		goto err_dbus;

	r = launcher_ck2_take_control(wl);
	if (r < 0)
		goto err_dbus_cleanup;

	r = launcher_ck2_activate(wl);
	if (r < 0)
		goto err_dbus_cleanup;

	weston_log("ck2: session control granted\n");
	* (struct launcher_ck2 **) out = wl;
	return 0;

err_dbus_cleanup:
	launcher_ck2_destroy_dbus(wl);
err_dbus:
	weston_dbus_close(wl->dbus, wl->dbus_ctx);
err_session:
	free(wl->sid);
err_seat:
	free(wl->seat);
err_wl:
	free(wl);
err_out:
	weston_log("ck2: cannot setup ConsoleKit2 helper (%d), using legacy fallback\n", r);
	errno = -r;
	return -1;
}

static void
launcher_ck2_destroy(struct weston_launcher *launcher)
{
	struct launcher_ck2 *wl = wl_container_of(launcher, wl, base);

	if (wl->pending_active) {
		dbus_pending_call_cancel(wl->pending_active);
		dbus_pending_call_unref(wl->pending_active);
	}

	launcher_ck2_release_control(wl);
	launcher_ck2_destroy_dbus(wl);
	weston_dbus_close(wl->dbus, wl->dbus_ctx);
	free(wl->sid);
	free(wl->seat);
	free(wl);
}

static int
launcher_ck2_get_vt(struct weston_launcher *launcher)
{
	struct launcher_ck2 *wl = wl_container_of(launcher, wl, base);
	return wl->vtnr;
}

const struct launcher_interface launcher_ck2_iface = {
	.connect = launcher_ck2_connect,
	.destroy = launcher_ck2_destroy,
	.open = launcher_ck2_open,
	.close = launcher_ck2_close,
	.activate_vt = launcher_ck2_activate_vt,
	.get_vt = launcher_ck2_get_vt,
};
