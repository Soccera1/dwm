#ifndef MULTIMEDIA_H
#define MULTIMEDIA_H

enum MultimediaAction {
	MultimediaVolumeDown,
	MultimediaVolumeUp,
	MultimediaVolumeMute,
	MultimediaMicMute,
	MultimediaPlayPause,
	MultimediaPause,
	MultimediaStop,
	MultimediaPrevious,
	MultimediaNext,
	MultimediaRewind,
	MultimediaForward,
	MultimediaBrightnessDown,
	MultimediaBrightnessUp,
	MultimediaKeyboardBrightnessDown,
	MultimediaKeyboardBrightnessUp,
};

void multimedia_run(enum MultimediaAction action);

#endif
