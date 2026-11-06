#pragma once

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#else
#include <SDL.h>
#endif

namespace devilution {

// Returns false when a PS3-specific input event is consumed (R3 toggle or
// right-stick axis input while mouse mode is active).
bool PS3HandleInputEvent(const SDL_Event &event);

// Produces one synthetic mouse-motion event from the current right-stick
// position. Called when the SDL event queue is empty.
bool PS3CreateMouseMotionEvent(SDL_Event *event);

// Whether the right stick is currently driving the mouse pointer.
bool PS3IsMouseControlEnabled();

} // namespace devilution
