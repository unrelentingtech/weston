#ifndef DESKTOP_SHELL_API_H
#define DESKTOP_SHELL_API_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <unistd.h>

#include "plugin-registry.h"

struct weston_compositor;
struct weston_view;
struct weston_seat;
struct desktop_shell;

#define WESTON_DESKTOP_SHELL_API_NAME "weston_desktop_shell_v1"

/** The weston-desktop-shell API
 *
 * This API allows control of the desktop shell module.
 * The module must be loaded at runtime with \a weston_compositor_load_desktop_shell,
 * after which the API can be retrieved by using \a weston_desktop_shell_get_api.
 */
struct weston_desktop_shell_api {
	/** Retrieve the desktop_shell context object.
	 *
	 * Note that this function does not create a new object, but always
	 * returns the same object per compositor instance.
	 * This function cannot fail while this API object is valid.
	 *
	 * \param compositor The compositor instance.
	 */
	struct desktop_shell *
	(*get)(struct weston_compositor *compositor);

	/** Activate a desktop shell surface.
	 *
	 * \param desktop_shell The desktop_shell context object.
	 * \param exit_status The exit status of the desktop_shell server process.
	 */
	void
	(*activate)(struct desktop_shell *shell, struct weston_view *view,
			struct weston_seat *seat, uint32_t flags);

	/** Change the function used for calculating an output's work area,
	 *  i.e. the output size minus panels.
	 *
	 * \param desktop_shell The desktop_shell context object.
	 * \param fn The function that should be used.
	 */
	void
	(*set_output_work_area_fn)(struct desktop_shell *shell,
			void (*fn)(struct desktop_shell *shell, struct weston_output *output,
				pixman_rectangle32_t *area));

};

/** Retrieve the API object for the desktop shell module.
 *
 * The module must have been previously loaded.
 *
 * \param compositor The compositor instance.
 */
static inline const struct weston_desktop_shell_api *
weston_desktop_shell_get_api(struct weston_compositor *compositor)
{
	const void *api;
	api = weston_plugin_api_get(compositor, WESTON_DESKTOP_SHELL_API_NAME,
						sizeof(struct weston_desktop_shell_api));
	/* The cast is necessary to use this function in C++ code */
	return (const struct weston_desktop_shell_api *)api;
}
#ifdef  __cplusplus
}
#endif

#endif
