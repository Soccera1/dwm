#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef MULTIMEDIA_ALSA
#include <alsa/asoundlib.h>
#endif

#ifdef MULTIMEDIA_DBUS
#include <gio/gio.h>
#endif

#ifdef MULTIMEDIA_XRANDR
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#endif

#include "multimedia.h"

#define BRIGHTNESS_STEP 5
#define DBUS_TIMEOUT_MS 750
#define MPRIS_PREFIX "org.mpris.MediaPlayer2."

#ifdef MULTIMEDIA_ALSA
static snd_mixer_elem_t *
mixer_element(snd_mixer_t **mixer, const char *first, const char *second)
{
	snd_mixer_selem_id_t *id;
	snd_mixer_elem_t *element = NULL;

	if (snd_mixer_open(mixer, 0) < 0 ||
	    snd_mixer_attach(*mixer, "default") < 0 ||
	    snd_mixer_selem_register(*mixer, NULL, NULL) < 0 ||
	    snd_mixer_load(*mixer) < 0)
		goto fail;
	if (snd_mixer_selem_id_malloc(&id) < 0)
		goto fail;
	snd_mixer_selem_id_set_index(id, 0);
	snd_mixer_selem_id_set_name(id, first);
	element = snd_mixer_find_selem(*mixer, id);
	if (!element && second) {
		snd_mixer_selem_id_set_name(id, second);
		element = snd_mixer_find_selem(*mixer, id);
	}
	snd_mixer_selem_id_free(id);
	if (element)
		return element;

fail:
	if (*mixer) {
		snd_mixer_close(*mixer);
		*mixer = NULL;
	}
	return NULL;
}

static void
change_volume(int direction)
{
	snd_mixer_t *mixer = NULL;
	snd_mixer_elem_t *element;
	snd_mixer_selem_channel_id_t channel;
	long minimum, maximum, current, step, value;

	element = mixer_element(&mixer, "Master", "PCM");
	channel = element && snd_mixer_selem_has_playback_channel(element,
	    SND_MIXER_SCHN_FRONT_LEFT) ? SND_MIXER_SCHN_FRONT_LEFT :
	    SND_MIXER_SCHN_MONO;
	if (!element ||
	    snd_mixer_selem_get_playback_volume_range(element, &minimum, &maximum) < 0 ||
	    snd_mixer_selem_get_playback_volume(element, channel, &current) < 0)
		goto done;
	step = (maximum - minimum) * BRIGHTNESS_STEP / 100;
	if (step < 1)
		step = 1;
	value = current + direction * step;
	if (value < minimum)
		value = minimum;
	if (value > maximum)
		value = maximum;
	snd_mixer_selem_set_playback_volume_all(element, value);

done:
	if (mixer)
		snd_mixer_close(mixer);
}

static void
toggle_mixer_switch(int capture)
{
	snd_mixer_t *mixer = NULL;
	snd_mixer_elem_t *element;
	snd_mixer_selem_channel_id_t channel;
	int enabled;

	element = mixer_element(&mixer, capture ? "Capture" : "Master",
	    capture ? "Mic" : "PCM");
	if (!element)
		return;
	if (capture) {
		channel = snd_mixer_selem_has_capture_channel(element,
		    SND_MIXER_SCHN_FRONT_LEFT) ? SND_MIXER_SCHN_FRONT_LEFT :
		    SND_MIXER_SCHN_MONO;
		if (snd_mixer_selem_has_capture_switch(element) &&
		    snd_mixer_selem_get_capture_switch(element, channel, &enabled) >= 0)
			snd_mixer_selem_set_capture_switch_all(element, !enabled);
	} else {
		channel = snd_mixer_selem_has_playback_channel(element,
		    SND_MIXER_SCHN_FRONT_LEFT) ? SND_MIXER_SCHN_FRONT_LEFT :
		    SND_MIXER_SCHN_MONO;
		if (snd_mixer_selem_has_playback_switch(element) &&
		    snd_mixer_selem_get_playback_switch(element, channel, &enabled) >= 0)
			snd_mixer_selem_set_playback_switch_all(element, !enabled);
	}
	snd_mixer_close(mixer);
}
#endif

#ifdef MULTIMEDIA_DBUS
static GVariant *
dbus_call(GDBusConnection *connection, const char *destination,
    const char *path, const char *interface, const char *method,
    GVariant *parameters, const GVariantType *reply_type)
{
	GError *error = NULL;
	GVariant *reply;

	reply = g_dbus_connection_call_sync(connection, destination, path,
	    interface, method, parameters, reply_type, G_DBUS_CALL_FLAGS_NONE,
	    DBUS_TIMEOUT_MS, NULL, &error);
	if (error)
		g_error_free(error);
	return reply;
}

static char *
mpris_player(GDBusConnection *connection)
{
	GVariant *reply, *status_reply, *status;
	GVariantIter *names;
	const char *name;
	char *fallback = NULL;

	reply = dbus_call(connection, "org.freedesktop.DBus",
	    "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames",
	    NULL, G_VARIANT_TYPE("(as)"));
	if (!reply)
		return NULL;
	g_variant_get(reply, "(as)", &names);
	while (g_variant_iter_loop(names, "&s", &name)) {
		if (strncmp(name, MPRIS_PREFIX, strlen(MPRIS_PREFIX)) != 0)
			continue;
		if (!fallback)
			fallback = g_strdup(name);
		status_reply = dbus_call(connection, name, "/org/mpris/MediaPlayer2",
		    "org.freedesktop.DBus.Properties", "Get",
		    g_variant_new("(ss)", "org.mpris.MediaPlayer2.Player",
		        "PlaybackStatus"), G_VARIANT_TYPE("(v)"));
		if (!status_reply)
			continue;
		g_variant_get(status_reply, "(v)", &status);
		if (g_variant_is_of_type(status, G_VARIANT_TYPE_STRING) &&
		    strcmp(g_variant_get_string(status, NULL), "Playing") == 0) {
			g_free(fallback);
			fallback = g_strdup(name);
			g_variant_unref(status);
			g_variant_unref(status_reply);
			break;
		}
		g_variant_unref(status);
		g_variant_unref(status_reply);
	}
	g_variant_iter_free(names);
	g_variant_unref(reply);
	return fallback;
}

static void
player_action(enum MultimediaAction action)
{
	GDBusConnection *connection;
	GError *error = NULL;
	GVariant *reply, *parameters = NULL;
	const char *method;
	char *player;

	connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
	if (!connection) {
		if (error)
			g_error_free(error);
		return;
	}
	player = mpris_player(connection);
	if (!player)
		goto done;
	switch (action) {
	case MultimediaPlayPause: method = "PlayPause"; break;
	case MultimediaPause:    method = "Pause"; break;
	case MultimediaStop:     method = "Stop"; break;
	case MultimediaPrevious: method = "Previous"; break;
	case MultimediaNext:     method = "Next"; break;
	case MultimediaRewind:
		method = "Seek";
		parameters = g_variant_new("(x)", (gint64)-10000000);
		break;
	case MultimediaForward:
		method = "Seek";
		parameters = g_variant_new("(x)", (gint64)10000000);
		break;
	default:
		goto done;
	}
	reply = dbus_call(connection, player, "/org/mpris/MediaPlayer2",
	    "org.mpris.MediaPlayer2.Player", method, parameters, NULL);
	if (reply)
		g_variant_unref(reply);

done:
	g_free(player);
	g_object_unref(connection);
}

static int
logind_set_brightness(const char *subsystem, const char *device,
    unsigned int value)
{
	GDBusConnection *connection;
	GError *error = NULL;
	GVariant *reply;
	const char *session_id, *session_path;
	int success = 0;

	session_id = getenv("XDG_SESSION_ID");
	if (!session_id)
		return 0;
	connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
	if (!connection) {
		if (error)
			g_error_free(error);
		return 0;
	}
	reply = dbus_call(connection, "org.freedesktop.login1",
	    "/org/freedesktop/login1", "org.freedesktop.login1.Manager",
	    "GetSession", g_variant_new("(s)", session_id),
	    G_VARIANT_TYPE("(o)"));
	if (!reply)
		goto done;
	g_variant_get(reply, "(&o)", &session_path);
	{
		GVariant *set_reply = dbus_call(connection, "org.freedesktop.login1",
		    session_path, "org.freedesktop.login1.Session", "SetBrightness",
		    g_variant_new("(ssu)", subsystem, device, value), NULL);
		if (set_reply) {
			success = 1;
			g_variant_unref(set_reply);
		}
	}
	g_variant_unref(reply);

done:
	g_object_unref(connection);
	return success;
}
#endif

static int
read_number(const char *path, long *value)
{
	FILE *file;
	int result;

	file = fopen(path, "r");
	if (!file)
		return 0;
	result = fscanf(file, "%ld", value) == 1;
	fclose(file);
	return result;
}

static int
write_number(const char *path, long value)
{
	FILE *file;
	int result;

	file = fopen(path, "w");
	if (!file)
		return 0;
	result = fprintf(file, "%ld\n", value) > 0;
	if (fclose(file) != 0)
		result = 0;
	return result;
}

static int
sysfs_brightness(const char *pattern, const char *subsystem, int direction)
{
	glob_t devices;
	char brightness[512], maximum_path[512];
	const char *device;
	long current, maximum, step, value;
	size_t i;

	if (glob(pattern, 0, NULL, &devices) != 0)
		return 0;
	for (i = 0; i < devices.gl_pathc; i++) {
		if (snprintf(brightness, sizeof brightness, "%s/brightness",
		    devices.gl_pathv[i]) >= (int)sizeof brightness ||
		    snprintf(maximum_path, sizeof maximum_path, "%s/max_brightness",
		    devices.gl_pathv[i]) >= (int)sizeof maximum_path ||
		    !read_number(brightness, &current) ||
		    !read_number(maximum_path, &maximum))
			continue;
		step = (maximum + 19) / 20;
		value = current + direction * step;
		if (value < 1)
			value = 1;
		if (value > maximum)
			value = maximum;
		if (write_number(brightness, value)) {
			globfree(&devices);
			return 1;
		}
#ifdef MULTIMEDIA_DBUS
		device = strrchr(devices.gl_pathv[i], '/');
		device = device ? device + 1 : devices.gl_pathv[i];
		if (logind_set_brightness(subsystem, device, (unsigned int)value)) {
			globfree(&devices);
			return 1;
		}
#else
		(void)device;
		(void)subsystem;
#endif
	}
	globfree(&devices);
	return 0;
}

#ifdef MULTIMEDIA_XRANDR
static int
xrandr_brightness(int direction)
{
	Display *display;
	XRRScreenResources *resources;
	XRRPropertyInfo *info;
	Atom property, actual_type;
	unsigned char *data = NULL;
	unsigned long items, bytes_after;
	long value, minimum, maximum, step;
	int format, changed = 0, i;

	display = XOpenDisplay(NULL);
	if (!display)
		return 0;
	resources = XRRGetScreenResourcesCurrent(display,
	    RootWindow(display, DefaultScreen(display)));
	if (!resources)
		goto done;
	property = XInternAtom(display, "Backlight", True);
	if (property == None)
		property = XInternAtom(display, "BACKLIGHT", True);
	if (property == None)
		goto free_resources;
	for (i = 0; i < resources->noutput; i++) {
		info = XRRQueryOutputProperty(display, resources->outputs[i], property);
		if (!info || !info->range || info->num_values < 2) {
			if (info)
				XFree(info);
			continue;
		}
		minimum = info->values[0];
		maximum = info->values[1];
		XFree(info);
		if (XRRGetOutputProperty(display, resources->outputs[i], property,
		    0, 4, False, False, XA_INTEGER, &actual_type, &format, &items,
		    &bytes_after, &data) != Success || actual_type != XA_INTEGER ||
		    format != 32 || items != 1 || !data) {
			if (data)
				XFree(data);
			data = NULL;
			continue;
		}
		value = *(long *)data;
		XFree(data);
		data = NULL;
		step = (maximum - minimum + 19) / 20;
		value += direction * step;
		if (value < minimum)
			value = minimum;
		if (value > maximum)
			value = maximum;
		XRRChangeOutputProperty(display, resources->outputs[i], property,
		    XA_INTEGER, 32, PropModeReplace, (unsigned char *)&value, 1);
		changed = 1;
	}
	XSync(display, False);

free_resources:
	XRRFreeScreenResources(resources);
done:
	XCloseDisplay(display);
	return changed;
}
#endif

static void
brightness_action(int keyboard, int direction)
{
	if (sysfs_brightness(keyboard ? "/sys/class/leds/*::kbd_backlight" :
	    "/sys/class/backlight/*", keyboard ? "leds" : "backlight", direction))
		return;
#ifdef MULTIMEDIA_XRANDR
	if (!keyboard)
		xrandr_brightness(direction);
#endif
}

void
multimedia_run(enum MultimediaAction action)
{
	switch (action) {
#ifdef MULTIMEDIA_ALSA
	case MultimediaVolumeDown: change_volume(-1); break;
	case MultimediaVolumeUp:   change_volume(1); break;
	case MultimediaVolumeMute: toggle_mixer_switch(0); break;
	case MultimediaMicMute:    toggle_mixer_switch(1); break;
#endif
#ifdef MULTIMEDIA_DBUS
	case MultimediaPlayPause:
	case MultimediaPause:
	case MultimediaStop:
	case MultimediaPrevious:
	case MultimediaNext:
	case MultimediaRewind:
	case MultimediaForward:
		player_action(action);
		break;
#endif
	case MultimediaBrightnessDown:
		brightness_action(0, -1);
		break;
	case MultimediaBrightnessUp:
		brightness_action(0, 1);
		break;
	case MultimediaKeyboardBrightnessDown:
		brightness_action(1, -1);
		break;
	case MultimediaKeyboardBrightnessUp:
		brightness_action(1, 1);
		break;
	default:
		break;
	}
}
