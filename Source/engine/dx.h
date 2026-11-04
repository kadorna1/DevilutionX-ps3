/**
 * @file dx.h
 *
 * Interface of functions setting up the graphics pipeline.
 */
#pragma once

#ifdef USE_SDL3
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#else
#include <SDL.h>
#endif

#include "engine/surface.hpp"

namespace devilution {

/** Whether we render directly to the screen surface, i.e. `PalSurface == GetOutputSurface()` */
extern bool RenderDirectlyToOutputSurface;

extern SDL_Surface *PalSurface;

Surface GlobalBackBuffer();

void dx_init();
void dx_cleanup();
void CreateBackBuffer();
void BltFast(SDL_Rect *srcRect, SDL_Rect *dstRect);
void Blit(SDL_Surface *src, SDL_Rect *srcRect, SDL_Rect *dstRect);
void RenderPresent();

#ifdef __PS3__
/** Timing checkpoints for direct indexed Smacker frame presentation. */
struct PS3FMVRenderStats {
	Uint32 textureSetupMs = 0;
	Uint32 textureLockMs = 0;
	Uint32 pixelExpandMs = 0;
	Uint32 renderCopyMs = 0;
	Uint32 renderPresentMs = 0;
};

/** Upload and present an indexed Smacker frame directly through the RSX texture. */
bool RenderIndexedVideoFrame(const Uint8 *pixels, int width, int height, const SDL_Color *palette, PS3FMVRenderStats *stats);
/** Upload and present an ARGB32 frame returned by Cell VDEC. */
bool RenderDecodedVideoFrame(const Uint8 *pixels, int width, int height, int sourcePitch);
#endif

} // namespace devilution
