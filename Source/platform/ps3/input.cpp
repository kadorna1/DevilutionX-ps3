#include "platform/ps3/input.h"

#include <algorithm>
#include <cmath>

#include <SDL.h>

#include "controls/controller_motion.h"
#include "diablo.h"
#include "utils/display.h"
#include "utils/log.hpp"

namespace devilution {
namespace {

bool MouseControlEnabled = true;
bool RightStickToggleHeld;
Uint32 PreviousMouseTick;
float MouseRemainderX;
float MouseRemainderY;

// Independent calibration: horizontal cursor travel is reduced while
// vertical travel is increased to compensate for the observed PS3 pad feel.
constexpr float MouseSpeedXPixelsPerSecond = 420.0F;
constexpr float MouseSpeedYPixelsPerSecond = 780.0F;
constexpr float MouseStickDeadzone = 0.04F;

bool IsRightStickAxis(Uint8 axis)
{
#if defined(JOY_AXIS_RIGHTX) && defined(JOY_AXIS_RIGHTY)
	return axis == JOY_AXIS_RIGHTX || axis == JOY_AXIS_RIGHTY;
#else
	(void)axis;
	return false;
#endif
}

} // namespace

bool PS3HandleInputEvent(const SDL_Event &event)
{
#if defined(JOY_BUTTON_RIGHTSTICK)
	if (event.type == SDL_JOYBUTTONDOWN && event.jbutton.button == JOY_BUTTON_RIGHTSTICK) {
		if (!RightStickToggleHeld) {
			RightStickToggleHeld = true;
			MouseControlEnabled = !MouseControlEnabled;
			PreviousMouseTick = SDL_GetTicks();
			MouseRemainderX = 0;
			MouseRemainderY = 0;
			LogInfo("PS3 input: right-stick mouse mode {}", MouseControlEnabled ? "enabled" : "disabled");
		}
		return false;
	}
	if (event.type == SDL_JOYBUTTONUP && event.jbutton.button == JOY_BUTTON_RIGHTSTICK) {
		RightStickToggleHeld = false;
		return false;
	}
#endif

	if (MouseControlEnabled && event.type == SDL_JOYAXISMOTION && IsRightStickAxis(event.jaxis.axis))
		return false;

	return true;
}

bool PS3CreateMouseMotionEvent(SDL_Event *event)
{
	const Uint32 now = SDL_GetTicks();
	if (!MouseControlEnabled) {
		PreviousMouseTick = now;
		MouseRemainderX = 0;
		MouseRemainderY = 0;
		return false;
	}
	if (PreviousMouseTick == 0) {
		PreviousMouseTick = now;
		return false;
	}
	const Uint32 elapsed = std::min<Uint32>(now - PreviousMouseTick, 50);
	PreviousMouseTick = now;
	if (elapsed == 0 || (std::fabs(rightStickX) < MouseStickDeadzone && std::fabs(rightStickY) < MouseStickDeadzone))
		return false;

	const float timeScale = static_cast<float>(elapsed) / 1000.0F;
	const float pendingX = rightStickX * MouseSpeedXPixelsPerSecond * timeScale + MouseRemainderX;
	// Controller motion stores positive rightStickY for an upward stick push,
	// while SDL mouse coordinates increase downwards.
	const float pendingY = -rightStickY * MouseSpeedYPixelsPerSecond * timeScale + MouseRemainderY;
	const int deltaX = static_cast<int>(pendingX);
	const int deltaY = static_cast<int>(pendingY);
	MouseRemainderX = pendingX - static_cast<float>(deltaX);
	MouseRemainderY = pendingY - static_cast<float>(deltaY);
	if (deltaX == 0 && deltaY == 0)
		return false;

	SDL_Surface *surface = GetOutputSurface();
	if (surface == nullptr || surface->w <= 0 || surface->h <= 0)
		return false;
	const int nextX = std::clamp(MousePosition.x + deltaX, 0, surface->w - 1);
	const int nextY = std::clamp(MousePosition.y + deltaY, 0, surface->h - 1);
	if (nextX == MousePosition.x && nextY == MousePosition.y) {
		MouseRemainderX = 0;
		MouseRemainderY = 0;
		return false;
	}

	*event = SDL_Event {};
	event->type = SDL_MOUSEMOTION;
	event->motion.type = SDL_MOUSEMOTION;
	event->motion.timestamp = now;
	event->motion.which = 0;
	// PollEvent returns this event directly, after SDL's queue conversion has
	// already happened, so use the game's logical coordinate space.
	event->motion.x = nextX;
	event->motion.y = nextY;
	event->motion.xrel = nextX - MousePosition.x;
	event->motion.yrel = nextY - MousePosition.y;
	return true;
}

bool PS3IsMouseControlEnabled()
{
	return MouseControlEnabled;
}

} // namespace devilution
