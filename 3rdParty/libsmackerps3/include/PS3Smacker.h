#pragma once

#include <cstdint>

#ifdef USE_SDL3
#include <SDL3/SDL_iostream.h>
#else
#include <SDL.h>
#endif

#include <SmackerDecoder.h>

namespace devilution {

struct PS3SmackerHandle {
	bool isValid = false;
	int instanceIndex = -1;
};

#ifdef USE_SDL3
PS3SmackerHandle PS3Smacker_Open(SDL_IOStream *stream);
#else
PS3SmackerHandle PS3Smacker_Open(SDL_RWops *stream);
#endif
void PS3Smacker_Close(PS3SmackerHandle &handle);
uint32_t PS3Smacker_GetNumAudioTracks(PS3SmackerHandle &handle);
SmackerAudioInfo PS3Smacker_GetAudioTrackDetails(PS3SmackerHandle &handle, uint32_t trackIndex);
uint32_t PS3Smacker_GetAudioData(PS3SmackerHandle &handle, uint32_t trackIndex, int16_t *data);
uint32_t PS3Smacker_GetNumFrames(PS3SmackerHandle &handle);
void PS3Smacker_GetFrameSize(PS3SmackerHandle &handle, uint32_t &width, uint32_t &height);
uint32_t PS3Smacker_GetCurrentFrameNum(PS3SmackerHandle &handle);
uint32_t PS3Smacker_GetNextFrame(PS3SmackerHandle &handle);
float PS3Smacker_GetFrameRate(PS3SmackerHandle &handle);
bool PS3Smacker_DidPaletteChange(PS3SmackerHandle &handle);
void PS3Smacker_GetPalette(PS3SmackerHandle &handle, uint8_t *palette);
void PS3Smacker_GetFrame(PS3SmackerHandle &handle, uint8_t *frame);
void PS3Smacker_Rewind(PS3SmackerHandle &handle);

} // namespace devilution
