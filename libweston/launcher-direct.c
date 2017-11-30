/*
 * Copyright © 2012 Benjamin Franzke
 * Copyright © 2013 Intel Corporation
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

#include "compositor.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#ifdef __linux__
#include <linux/vt.h>
#include <linux/kd.h>
#include <linux/major.h>
#elif __FreeBSD__
#include <sys/consio.h>
#include <sys/kbio.h>
#include <termios.h>
#endif

#include "launcher-impl.h"

#define DRM_MAJOR 226

#ifndef KDSKBMUTE
#define KDSKBMUTE	0x4B51
#endif

#ifdef __linux__
#define TTY_PATH	"/dev/tty%d"
/* major()/minor() */
#ifdef MAJOR_IN_MKDEV
#include <sys/mkdev.h>
#endif
#ifdef MAJOR_IN_SYSMACROS
#include <sys/sysmacros.h>
#endif
#elif __FreeBSD__
#define TTY_PATH	"/dev/ttyv%d"
#endif

#ifdef BUILD_DRM_COMPOSITOR

#include <xf86drm.h>

static inline int
is_drm_master(int drm_fd)
{
	drm_magic_t magic;

	return drmGetMagic(drm_fd, &magic) == 0 &&
		drmAuthMagic(drm_fd, magic) == 0;
}

#else

static inline int
drmDropMaster(int drm_fd)
{
	return 0;
}

static inline int
drmSetMaster(int drm_fd)
{
	return 0;
}

static inline int
is_drm_master(int drm_fd)
{
	return 0;
}

#endif

struct launcher_direct {
	struct weston_launcher base;
	struct weston_compositor *compositor;
	int kb_mode, tty, drm_fd;
	struct wl_event_source *vt_source;
};

static int
vt_handler(int signal_number, void *data)
{
	struct launcher_direct *launcher = data;
	struct weston_compositor *compositor = launcher->compositor;

	if (compositor->session_active) {
		compositor->session_active = 0;
		wl_signal_emit(&compositor->session_signal, compositor);
		drmDropMaster(launcher->drm_fd);
		ioctl(launcher->tty, VT_RELDISP, 1);
	} else {
		ioctl(launcher->tty, VT_RELDISP, VT_ACKACQ);
		drmSetMaster(launcher->drm_fd);
		compositor->session_active = 1;
		wl_signal_emit(&compositor->session_signal, compositor);
	}

	return 1;
}

static int
setup_tty(struct launcher_direct *launcher, int tty)
{
	struct wl_event_loop *loop;
	struct vt_mode mode = { 0 };
#ifdef __linux__
	struct stat buf;
#endif
	char tty_device[32] ="<stdin>";
	int ret, kd_mode, vt_num;

	if (tty == 0) {
		launcher->tty = dup(tty);
		if (launcher->tty == -1) {
			weston_log("couldn't dup stdin: %m\n");
			return -1;
		}
	} else {
		snprintf(tty_device, sizeof tty_device, TTY_PATH, tty);
		launcher->tty = open(tty_device, O_RDWR | O_CLOEXEC);
		if (launcher->tty == -1) {
			weston_log("couldn't open tty %s: %m\n", tty_device);
			return -1;
		}
	}

#ifdef __linux__
	if (fstat(launcher->tty, &buf) == -1 ||
	    major(buf.st_rdev) != TTY_MAJOR || minor(buf.st_rdev) == 0) {
		weston_log("%s not a vt\n", tty_device);
		weston_log("if running weston from ssh, "
			   "use --tty to specify a tty\n");
		goto err_close;
	}
	vt_num = minor(buf.st_rdev);
#elif __FreeBSD__
	ret = ioctl(launcher->tty, VT_GETINDEX, &vt_num);
	if (ret) {
		weston_log("couldn't get VT index for %s: %m\n", tty_device);
		return -1;
	}
#endif

	ret = ioctl(launcher->tty, KDGETMODE, &kd_mode);
	if (ret) {
		weston_log("failed to get VT mode: %m\n");
		return -1;
	}
	if (kd_mode != KD_TEXT) {
		weston_log("%s is already in graphics mode, "
			   "is another display server running?\n", tty_device);
		goto err_close;
	}

	ioctl(launcher->tty, VT_ACTIVATE, vt_num);
	ioctl(launcher->tty, VT_WAITACTIVE, vt_num);

	if (ioctl(launcher->tty, KDGKBMODE, &launcher->kb_mode)) {
		weston_log("failed to read keyboard mode: %m\n");
		goto err_close;
	}

#ifdef __linux__
	if (ioctl(launcher->tty, KDSKBMUTE, 1) &&
	    ioctl(launcher->tty, KDSKBMODE, K_OFF)) {
		weston_log("failed to set K_OFF keyboard mode: %m\n");
		goto err_close;
	}
#elif __FreeBSD__
	if (ioctl(launcher->tty, KDSKBMODE, K_RAW) == -1) {
		weston_log("failed to set K_RAW keyboard mode: %m\n");
		goto err_close;
	}

	/* Put the tty into raw mode */
	struct termios tios;
	if (tcgetattr(launcher->tty, &tios)) {
		weston_log("Failed to get terminal attribute: %m\n");
		goto err_close;
	}
	cfmakeraw(&tios);
	if (tcsetattr(launcher->tty, TCSAFLUSH, &tios)) {
		weston_log("Failed to set terminal attribute: %m\n");
		goto err_close;
	}
#endif

	ret = ioctl(launcher->tty, KDSETMODE, KD_GRAPHICS);
	if (ret) {
		weston_log("failed to set KD_GRAPHICS mode on tty: %m\n");
		goto err_close;
	}

	mode.mode = VT_PROCESS;
	mode.relsig = SIGUSR2;
	mode.acqsig = SIGUSR2;
#ifdef __FreeBSD__
	mode.frsig = SIGIO; /* not used, but has to be set anyway */
#endif
	if (ioctl(launcher->tty, VT_SETMODE, &mode) < 0) {
		weston_log("failed to take control of vt handling\n");
		goto err_close;
	}

	loop = wl_display_get_event_loop(launcher->compositor->wl_display);
	launcher->vt_source =
		wl_event_loop_add_signal(loop, SIGUSR2, vt_handler, launcher);
	if (!launcher->vt_source)
		goto err_close;

	return 0;

 err_close:
	close(launcher->tty);
	return -1;
}

static int
launcher_direct_open(struct weston_launcher *launcher_base, const char *path, int flags)
{
	struct launcher_direct *launcher = wl_container_of(launcher_base, launcher, base);
	struct stat s;
	int fd;

	fd = open(path, flags | O_CLOEXEC);
	if (fd == -1)
		return -1;

	if (fstat(fd, &s) == -1) {
		close(fd);
		return -1;
	}

#ifdef __linux__
	if (major(s.st_rdev) == DRM_MAJOR) {
#endif
		launcher->drm_fd = fd;
		if (!is_drm_master(fd)) {
			weston_log("drm fd not master\n");
			close(fd);
			return -1;
		}
#ifdef __linux__
	}
#endif

	return fd;
}

static void
launcher_direct_close(struct weston_launcher *launcher_base, int fd)
{
	close(fd);
}

static void
launcher_direct_restore(struct weston_launcher *launcher_base)
{
	struct launcher_direct *launcher = wl_container_of(launcher_base, launcher, base);
	struct vt_mode mode = { 0 };

	if (
#ifdef __linux__
	    ioctl(launcher->tty, KDSKBMUTE, 0) &&
#endif
	    ioctl(launcher->tty, KDSKBMODE, launcher->kb_mode)) {
		weston_log("failed to restore kb mode: %m\n");
	}

	if (ioctl(launcher->tty, KDSETMODE, KD_TEXT))
		weston_log("failed to set KD_TEXT mode on tty: %m\n");

#ifdef __FreeBSD__
	/* Restore sane mode */
	struct termios tios;
	if (tcgetattr(launcher->tty, &tios)) {
		weston_log("Failed to get terminal attribute: %m\n");
	} else {
		cfmakesane(&tios);
		if (tcsetattr(launcher->tty, TCSAFLUSH, &tios)) {
			weston_log("Failed to set terminal attribute: %m\n");
		}
	}
#endif

	/* We have to drop master before we switch the VT back in
	 * VT_AUTO, so we don't risk switching to a VT with another
	 * display server, that will then fail to set drm master. */
	drmDropMaster(launcher->drm_fd);

	mode.mode = VT_AUTO;
	if (ioctl(launcher->tty, VT_SETMODE, &mode) < 0)
		weston_log("could not reset vt handling\n");
}

static int
launcher_direct_activate_vt(struct weston_launcher *launcher_base, int vt)
{
	struct launcher_direct *launcher = wl_container_of(launcher_base, launcher, base);
	return ioctl(launcher->tty, VT_ACTIVATE, vt);
}

static int
launcher_direct_connect(struct weston_launcher **out, struct weston_compositor *compositor,
			int tty, const char *seat_id, bool sync_drm)
{
	struct launcher_direct *launcher;

	if (geteuid() != 0)
		return -EINVAL;

	launcher = zalloc(sizeof(*launcher));
	if (launcher == NULL)
		return -ENOMEM;

	launcher->base.iface = &launcher_direct_iface;
	launcher->compositor = compositor;

	if (setup_tty(launcher, tty) == -1) {
		free(launcher);
		return -1;
	}

	* (struct launcher_direct **) out = launcher;
	return 0;
}

static void
launcher_direct_destroy(struct weston_launcher *launcher_base)
{
	struct launcher_direct *launcher = wl_container_of(launcher_base, launcher, base);

	launcher_direct_restore(&launcher->base);
	wl_event_source_remove(launcher->vt_source);

	if (launcher->tty >= 0)
		close(launcher->tty);

	free(launcher);
}

static int
launcher_direct_get_vt(struct weston_launcher *base)
{
	struct launcher_direct *launcher = wl_container_of(base, launcher, base);
	struct stat s;
	if (fstat(launcher->tty, &s) < 0)
		return -1;

	return minor(s.st_rdev);
}

const struct launcher_interface launcher_direct_iface = {
	.connect = launcher_direct_connect,
	.destroy = launcher_direct_destroy,
	.open = launcher_direct_open,
	.close = launcher_direct_close,
	.activate_vt = launcher_direct_activate_vt,
	.get_vt = launcher_direct_get_vt,
};
