#pragma once

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#else
#include <SDL.h>
#endif

#include "controls/controller.h"
#include "controls/controller_motion.h"

#ifdef __PS3__
#include "platform/ps3/input.h"
#endif

namespace devilution {

inline bool PollEvent(SDL_Event *event)
{
#ifdef USE_SDL3
	while (SDL_PollEvent(event)) {
#else
	while (SDL_PollEvent(event) != 0) {
#endif
		UnlockControllerState(*event);
		ProcessControllerMotion(*event);
#ifdef __PS3__
		// R3 toggles right-stick mouse mode; consume right-stick axes while enabled.
		if (!PS3HandleInputEvent(*event))
			continue;
#endif
		return true;
	}
#ifdef __PS3__
	// Continue generating pointer movement while an analog stick is held, even
	// when the SDL joystick backend only emits events when an axis value changes.
	return PS3CreateMouseMotionEvent(event);
#else
	return false;
#endif
}

} // namespace devilution
