#pragma once
#include <QString>

#ifdef Q_OS_LINUX
// Picks the ALSA audio-device string mpv should target on this Pi. ALSA's
// "default" pcm can resolve to whatever card happened to enumerate first
// (e.g. a USB dongle's stub audio interface), not the real HDMI output, so
// callers use this instead of leaving mpv on "default". Returns an empty
// string if no vc4-hdmi card is found (e.g. running on non-RPi hardware),
// in which case the caller should leave mpv's audio-device alone.
QString detectAlsaAudioDevice();
#endif
