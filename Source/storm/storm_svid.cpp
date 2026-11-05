#include "storm/storm_svid.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>

#ifdef USE_SDL3
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_timer.h>

#ifndef NOSOUND
#include <SDL3/SDL_audio.h>
#include <SDL3_mixer/SDL_mixer.h>
#endif
#else
#include <SDL.h>

#ifndef NOSOUND
#include "utils/aulib.hpp"
#include "utils/push_aulib_decoder.h"
#endif
#endif

#include <SmackerDecoder.h>
#ifdef __PS3__
#include <PS3Smacker.h>
#endif

#include "engine/assets.hpp"
#include "engine/dx.h"
#include "engine/palette.h"
#include "engine/sound.h"
#include "options.h"
#include "utils/display.h"
#include "utils/log.hpp"
#include "utils/sdl_compat.h"
#include "utils/sdl_wrap.h"

namespace devilution {
namespace {

#ifndef NOSOUND
#ifdef USE_SDL3
SDL_AudioStream *SVidAudioStream;
SDL_AudioDeviceID SVidAudioDevice;
bool SVidAutoStreamEnabled;
#else
std::optional<Aulib::Stream> SVidAudioStream;
PushAulibDecoder *SVidAudioDecoder;
#endif
std::uint8_t SVidAudioDepth;
std::unique_ptr<int16_t[]> SVidAudioBuffer;
#endif

// Smacker's atomic time unit is a one hundred thousand's of a second (i.e. 0.01 millisecond, or 10 microseconds).
// We use SDL ticks for timing, which have millisecond resolution.
// There are 100 Smacker time units in a millisecond.
constexpr uint64_t SmackerTimeUnit = 100;
constexpr uint64_t TimeMsToSmk(uint64_t ms) { return ms * SmackerTimeUnit; }
constexpr uint64_t TimeSmkToMs(uint64_t time) { return time / SmackerTimeUnit; };
uint64_t GetTicksSmk()
{
#if SDL_VERSION_ATLEAST(2, 0, 18) && !defined(USE_SDL3)
	return TimeMsToSmk(SDL_GetTicks64());
#else
	return TimeMsToSmk(SDL_GetTicks());
#endif
}

uint32_t SVidWidth, SVidHeight;
bool SVidLoop;
SmackerHandle SVidHandle;
#ifdef __PS3__
PS3SmackerHandle SVidPS3Handle;
bool SVidUsePS3Smacker = false;
#endif
std::unique_ptr<uint8_t[]> SVidFrameBuffer;
SDLPaletteUniquePtr SVidPalette;
SDLSurfaceUniquePtr SVidSurface;

// The end of the current frame (time in SMK time units from the start of the program).
uint64_t SVidFrameEnd;
// The length of a frame in SMK time units.
uint32_t SVidFrameLength;
#ifdef __PS3__
uint32_t SVidDebugContinueCount;
#endif

bool SVidDecoderIsValid()
{
#ifdef __PS3__
	return SVidUsePS3Smacker ? SVidPS3Handle.isValid : SVidHandle.isValid;
#else
	return SVidHandle.isValid;
#endif
}

bool SVidDecoderOpen(const char *filename)
{
#ifdef __PS3__
	SVidUsePS3Smacker = false;
	SVidPS3Handle = PS3Smacker_Open(OpenAssetAsSdlRwOps(filename));
	if (SVidPS3Handle.isValid) {
		SVidUsePS3Smacker = true;
		LogInfo("PS3 FMV: decoder backend=JonnyH libsmacker C (HDD disk-streaming)");
		return true;
	}
	LogWarn("PS3 FMV: JonnyH libsmacker failed to open {}; retrying with libsmackerdec", filename);
#endif
	SVidHandle = Smacker_Open(OpenAssetAsSdlRwOps(filename));
#ifdef __PS3__
	SVidUsePS3Smacker = false;
#endif
	return SVidHandle.isValid;
}

void SVidDecoderClose()
{
#ifdef __PS3__
	if (SVidUsePS3Smacker) {
		PS3Smacker_Close(SVidPS3Handle);
		SVidUsePS3Smacker = false;
		return;
	}
#endif
	if (SVidHandle.isValid)
		Smacker_Close(SVidHandle);
}

SmackerAudioInfo SVidDecoderGetAudioTrackDetails(uint32_t trackIndex)
{
#ifdef __PS3__
	if (SVidUsePS3Smacker)
		return PS3Smacker_GetAudioTrackDetails(SVidPS3Handle, trackIndex);
#endif
	return Smacker_GetAudioTrackDetails(SVidHandle, trackIndex);
}

uint32_t SVidDecoderGetAudioData(uint32_t trackIndex, int16_t *data)
{
#ifdef __PS3__
	if (SVidUsePS3Smacker)
		return PS3Smacker_GetAudioData(SVidPS3Handle, trackIndex, data);
#endif
	return Smacker_GetAudioData(SVidHandle, trackIndex, data);
}

uint32_t SVidDecoderGetNumFrames()
{
#ifdef __PS3__
	if (SVidUsePS3Smacker)
		return PS3Smacker_GetNumFrames(SVidPS3Handle);
#endif
	return Smacker_GetNumFrames(SVidHandle);
}

uint32_t SVidDecoderGetCurrentFrameNum()
{
#ifdef __PS3__
	if (SVidUsePS3Smacker)
		return PS3Smacker_GetCurrentFrameNum(SVidPS3Handle);
#endif
	return Smacker_GetCurrentFrameNum(SVidHandle);
}

uint32_t SVidDecoderGetNextFrame()
{
#ifdef __PS3__
	if (SVidUsePS3Smacker)
		return PS3Smacker_GetNextFrame(SVidPS3Handle);
#endif
	return Smacker_GetNextFrame(SVidHandle);
}

float SVidDecoderGetFrameRate()
{
#ifdef __PS3__
	if (SVidUsePS3Smacker)
		return PS3Smacker_GetFrameRate(SVidPS3Handle);
#endif
	return Smacker_GetFrameRate(SVidHandle);
}

bool SVidDecoderDidPaletteChange()
{
#ifdef __PS3__
	if (SVidUsePS3Smacker)
		return PS3Smacker_DidPaletteChange(SVidPS3Handle);
#endif
	return Smacker_DidPaletteChange(SVidHandle);
}

void SVidDecoderGetPalette(uint8_t *palette)
{
#ifdef __PS3__
	if (SVidUsePS3Smacker) {
		PS3Smacker_GetPalette(SVidPS3Handle, palette);
		return;
	}
#endif
	Smacker_GetPalette(SVidHandle, palette);
}

void SVidDecoderGetFrame(uint8_t *frame)
{
#ifdef __PS3__
	if (SVidUsePS3Smacker) {
		PS3Smacker_GetFrame(SVidPS3Handle, frame);
		return;
	}
#endif
	Smacker_GetFrame(SVidHandle, frame);
}

void SVidDecoderRewind()
{
#ifdef __PS3__
	if (SVidUsePS3Smacker) {
		PS3Smacker_Rewind(SVidPS3Handle);
		return;
	}
#endif
	Smacker_Rewind(SVidHandle);
}

void SVidDecoderGetFrameSize(uint32_t &width, uint32_t &height)
{
#ifdef __PS3__
	if (SVidUsePS3Smacker) {
		PS3Smacker_GetFrameSize(SVidPS3Handle, width, height);
		return;
	}
#endif
	Smacker_GetFrameSize(SVidHandle, width, height);
}

bool IsLandscapeFit(unsigned long srcW, unsigned long srcH, unsigned long dstW, unsigned long dstH)
{
	return srcW * dstH > dstW * srcH;
}

#ifdef USE_SDL1
// Whether we've changed the video mode temporarily for SVid.
// If true, we must restore it once the video has finished playing.
bool IsSVidVideoMode = false;

// Set the video mode close to the SVid resolution while preserving aspect ratio.
void TrySetVideoModeToSVidForSDL1()
{
	const SDL_Surface *display = SDL_GetVideoSurface();
#if defined(SDL1_VIDEO_MODE_SVID_FLAGS)
	const int flags = SDL1_VIDEO_MODE_SVID_FLAGS;
#elif defined(SDL1_VIDEO_MODE_FLAGS)
	const int flags = SDL1_VIDEO_MODE_FLAGS;
#else
	const int flags = display->flags;
#endif
#ifdef SDL1_FORCE_SVID_VIDEO_MODE
	IsSVidVideoMode = true;
#else
	IsSVidVideoMode = (flags & (SDL_FULLSCREEN | SDL_NOFRAME)) != 0;
#endif
	if (!IsSVidVideoMode)
		return;

	int w;
	int h;
	if (IsLandscapeFit(SVidWidth, SVidHeight, display->w, display->h)) {
		w = SVidWidth;
		h = SVidWidth * display->h / display->w;
	} else {
		w = SVidHeight * display->w / display->h;
		h = SVidHeight;
	}

#ifndef SDL1_FORCE_SVID_VIDEO_MODE
	if (!SDL_VideoModeOK(w, h, /*bpp=*/display->format->BitsPerPixel, flags)) {
		IsSVidVideoMode = false;

		// Get available fullscreen/hardware modes
		SDL_Rect **modes = SDL_ListModes(nullptr, flags);

		// Check is there are any modes available.
		if (modes == reinterpret_cast<SDL_Rect **>(0)
		    || modes == reinterpret_cast<SDL_Rect **>(-1)) {
			return;
		}

		// Search for a usable video mode
		bool found = false;
		for (int i = 0; modes[i]; i++) {
			if (modes[i]->w == w || modes[i]->h == h) {
				found = true;
				break;
			}
		}
		if (!found)
			return;
		IsSVidVideoMode = true;
	}
#endif
	SetVideoMode(w, h, display->format->BitsPerPixel, flags);
}
#endif

#ifndef NOSOUND
// Returns the volume scaled to [0.0F, 1.0F] range.
float GetVolume() { return static_cast<float>(*GetOptions().Audio.soundVolume - VOLUME_MIN) / -VOLUME_MIN; }

bool ShouldPushAudioData()
{
#ifdef USE_SDL3
	return SVidAudioStream != nullptr && SVidAutoStreamEnabled;
#else
	return SVidAudioStream && SVidAudioStream->isPlaying();
#endif
}
#endif

bool SVidLoadNextFrame()
{
#ifdef __PS3__
	const uint32_t traceIndex = SVidDebugContinueCount == 0 ? 0 : SVidDebugContinueCount - 1;
	const uint32_t currentFrame = SVidDecoderGetCurrentFrameNum();
	LogInfo("PS3 FMV ADVANCE: begin call={} current={} total={}",
	    traceIndex, currentFrame, SVidDecoderGetNumFrames());
#endif
	if (SVidDecoderGetCurrentFrameNum() >= SVidDecoderGetNumFrames()) {
		if (!SVidLoop) {
#ifdef __PS3__
			LogInfo("PS3 FMV ADVANCE: movie end call={}", traceIndex);
#endif
			return false;
		}
#ifdef __PS3__
		LogInfo("PS3 FMV ADVANCE: rewind begin call={}", traceIndex);
#endif
		SVidDecoderRewind();
#ifdef __PS3__
		LogInfo("PS3 FMV ADVANCE: rewind complete call={}", traceIndex);
#endif
	}

	SVidFrameEnd += SVidFrameLength;
#ifdef __PS3__
	LogInfo("PS3 FMV ADVANCE: decoder call begin call={} current={}",
	    traceIndex, SVidDecoderGetCurrentFrameNum());
#endif
	const uint32_t decodedFrame = SVidDecoderGetNextFrame();
#ifdef __PS3__
	LogInfo("PS3 FMV ADVANCE: decoder call complete call={} result={} current={}",
	    traceIndex,
	    decodedFrame == std::numeric_limits<uint32_t>::max() ? -1 : static_cast<int>(decodedFrame),
	    SVidDecoderGetCurrentFrameNum());
#endif
	if (decodedFrame == std::numeric_limits<uint32_t>::max()) {
#ifdef __PS3__
		LogError("PS3 FMV: decoder reported an error at frame index {}", traceIndex);
#else
		LogError("Smacker decoder reported an error while advancing to the next frame");
#endif
		return false;
	}
#ifdef __PS3__
	LogInfo("PS3 FMV ADVANCE: frame copy begin call={} decoded={}", traceIndex, decodedFrame);
#endif
	SVidDecoderGetFrame(SVidFrameBuffer.get());
#ifdef __PS3__
	LogInfo("PS3 FMV ADVANCE: frame copy complete call={} decoded={}", traceIndex, decodedFrame);
#endif

	return true;
}

void UpdatePalette()
{
	constexpr size_t NumColors = 256;
	uint8_t paletteData[NumColors * 3];
	SVidDecoderGetPalette(paletteData);

	SDL_Color *colors = SVidPalette->colors;
	for (unsigned i = 0; i < NumColors; ++i) {
		colors[i].r = paletteData[i * 3];
		colors[i].g = paletteData[(i * 3) + 1];
		colors[i].b = paletteData[(i * 3) + 2];
#ifndef USE_SDL1
		colors[i].a = SDL_ALPHA_OPAQUE;
#endif
	}

#ifdef USE_SDL1
#if SDL1_VIDEO_MODE_BPP == 8
	// When the video surface is 8bit, we need to set the output palette.
	SDL_SetColors(SDL_GetVideoSurface(), colors, 0, NumColors);
#endif
	if (SDL_SetPalette(SVidSurface.get(), SDL_LOGPAL, colors, 0, NumColors) <= 0) {
		ErrSdl();
	}
#else
	if (!SDLC_SetSurfacePalette(SVidSurface.get(), SVidPalette.get())) {
		ErrSdl();
	}

	const SDL_Surface *surface = GetOutputSurface();
	if (SDLC_SURFACE_BITSPERPIXEL(surface) == 8) {
		if (!SDLC_SetSurfacePalette(GetOutputSurface(), SVidPalette.get())) {
			ErrSdl();
		}
	}
#endif
}

bool BlitFrame(
#ifdef __PS3__
    PS3FMVRenderStats *renderStats
#else
    bool
#endif
)
{
#ifdef __PS3__
	// Write palette-expanded pixels straight into the streaming RSX texture.
	// This avoids an indexed SDL surface blit followed by a second full-frame
	// software-surface-to-texture copy in RenderPresent().
	if (renderer != nullptr && RenderIndexedVideoFrame(SVidFrameBuffer.get(), static_cast<int>(SVidWidth), static_cast<int>(SVidHeight), SVidPalette->colors, renderStats))
		return true;
#endif
#ifndef USE_SDL1
	if (renderer != nullptr) {
		if (
#ifdef USE_SDL3
		    !SDL_BlitSurface(SVidSurface.get(), nullptr, GetOutputSurface(), nullptr)
#else
		    SDL_BlitSurface(SVidSurface.get(), nullptr, GetOutputSurface(), nullptr) <= -1
#endif
		) {
			ErrSdl();
		}
	}

	RenderPresent();
	return true;
#else
	{
		SDL_Surface *outputSurface = GetOutputSurface();
#ifdef USE_SDL1
		const bool isIndexedOutputFormat = SDLBackport_IsPixelFormatIndexed(outputSurface->format);
#else
#ifdef USE_SDL3
		const SDL_PixelFormat wndFormat = SDL_GetWindowPixelFormat(ghMainWnd);
#else
		const Uint32 wndFormat = SDL_GetWindowPixelFormat(ghMainWnd);
#endif
		const bool isIndexedOutputFormat = SDL_ISPIXELFORMAT_INDEXED(wndFormat);
#endif
		SDL_Rect outputRect;
		if (isIndexedOutputFormat) {
			// Cannot scale if the output format is indexed (8-bit palette).
			outputRect.w = static_cast<int>(SVidWidth);
			outputRect.h = static_cast<int>(SVidHeight);
		} else if (IsLandscapeFit(SVidWidth, SVidHeight, outputSurface->w, outputSurface->h)) {
			outputRect.w = outputSurface->w;
			outputRect.h = SVidHeight * outputSurface->w / SVidWidth;
		} else {
			outputRect.w = SVidWidth * outputSurface->h / SVidHeight;
			outputRect.h = outputSurface->h;
		}
		outputRect.x = (outputSurface->w - outputRect.w) / 2;
		outputRect.y = (outputSurface->h - outputRect.h) / 2;

		if (isIndexedOutputFormat
		    || outputSurface->w == static_cast<int>(SVidWidth)
		    || outputSurface->h == static_cast<int>(SVidHeight)) {
			if (
#ifdef USE_SDL3
			    !SDL_BlitSurface(SVidSurface.get(), nullptr, outputSurface, &outputRect)
#else
			    SDL_BlitSurface(SVidSurface.get(), nullptr, outputSurface, &outputRect) <= -1
#endif
			) {
				ErrSdl();
			}
		} else {
			// The source surface is always 8-bit, and the output surface is never 8-bit in this branch.
			// We must convert to the output format before calling SDL_BlitScaled.
#ifdef USE_SDL1
			SDLSurfaceUniquePtr converted = SDLWrap::ConvertSurface(SVidSurface.get(), ghMainWnd->format, 0);
#else
			SDLSurfaceUniquePtr converted = SDLWrap::ConvertSurfaceFormat(SVidSurface.get(), wndFormat, 0);
#endif
			if (
#ifdef USE_SDL3
			    SDL_BlitSurfaceScaled(converted.get(), nullptr, outputSurface, &outputRect, SDL_SCALEMODE_LINEAR)
#else
			    SDL_BlitScaled(converted.get(), nullptr, outputSurface, &outputRect) <= -1
#endif
			) {
				Log("{}", SDL_GetError());
				return false;
			}
		}
	}

	RenderPresent();
	return true;
#endif
}

#if defined(USE_SDL3) && !defined(NOSOUND)
void SVidInitAudioStream(const SmackerAudioInfo &audioInfo)
{
	SVidAutoStreamEnabled = diablo_is_focused();
	SDL_AudioSpec srcSpec = {};
	srcSpec.channels = static_cast<int>(audioInfo.nChannels);
	srcSpec.freq = static_cast<int>(audioInfo.sampleRate);
	srcSpec.format = audioInfo.bitsPerSample == 8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE;
	SVidAudioStream = SDL_CreateAudioStream(&srcSpec, /*dstSpec=*/&srcSpec);
	if (SVidAudioStream == nullptr) {
		LogError(LogCategory::Audio, "SDL_CreateAudioStream (from SVidPlayBegin): {}", SDL_GetError());
		SDL_ClearError();
		SVidAudioStream = nullptr;
		return;
	}
	// We open our own audio device since the one created by SDL3_mixer isn't sufficient.
	SVidAudioDevice = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
	if (SVidAudioDevice == 0) {
		LogError(LogCategory::Audio, "SDL_OpenAudioDevice (from SVidPlayBegin): {}", SDL_GetError());
		SDL_ClearError();
		SDL_DestroyAudioStream(SVidAudioStream);
		SVidAudioStream = nullptr;
		return;
	}
	if (!SDL_BindAudioStream(SVidAudioDevice, SVidAudioStream)) {
		LogError(LogCategory::Audio, "SDL_BindAudioStream (from SVidPlayBegin): {}", SDL_GetError());
		SDL_ClearError();
		SDL_CloseAudioDevice(SVidAudioDevice);
		SVidAudioDevice = 0;
		SDL_DestroyAudioStream(SVidAudioStream);
		SVidAudioStream = nullptr;
		return;
	}
	if (!SDL_SetAudioStreamGain(SVidAudioStream, GetVolume())) {
		LogWarn(LogCategory::Audio, "SDL_SetAudioStreamGain (from SVidPlayBegin): {}", SDL_GetError());
		SDL_ClearError();
	}
}
#endif

} // namespace

bool SVidPlayBegin(const char *filename, int flags)
{
#ifdef __PS3__
	LogInfo("PS3 FMV: opening movie {}", filename);
#endif
	if ((flags & 0x10000) != 0 || (flags & 0x20000000) != 0) {
#ifdef __PS3__
		LogInfo("PS3 FMV: movie disabled by flags");
#endif
		return false;
	}

	SVidLoop = false;
#ifdef __PS3__
	SVidDebugContinueCount = 0;
#endif
	if ((flags & 0x40000) != 0)
		SVidLoop = true;
	// 0x8 // Non-interlaced
	// 0x200, 0x800 // Upscale video
	// 0x80000 // Center horizontally
	// 0x100000 // Disable video
	// 0x800000 // Edge detection
	// 0x200800 // Clear FB

	if (!SVidDecoderOpen(filename)) {
#ifdef __PS3__
		LogError("PS3 FMV: no available Smacker decoder could open {}", filename);
#endif
		return false;
	}
#ifdef __PS3__
	LogInfo("PS3 FMV: decoder opened {} backend={}", filename,
	    SVidUsePS3Smacker ? "JonnyH libsmacker C" : "diasurgical libsmackerdec C++");
#endif

#ifndef NOSOUND
	const bool enableAudio = (flags & 0x1000000) == 0;

	auto audioInfo = SVidDecoderGetAudioTrackDetails(0);
	LogVerbose(LogCategory::Audio, "SVid audio depth={} channels={} rate={}", audioInfo.bitsPerSample, audioInfo.nChannels, audioInfo.sampleRate);

	if (enableAudio && audioInfo.bitsPerSample != 0) {
		sound_stop(); // Stop in-progress music and sound effects

		SVidAudioDepth = audioInfo.bitsPerSample;
		SVidAudioBuffer = std::unique_ptr<int16_t[]> { new int16_t[audioInfo.idealBufferSize / 2] };
#ifdef __PS3__
		LogInfo("PS3 FMV: allocated audio buffer {} bytes", audioInfo.idealBufferSize);
#endif

#ifndef USE_SDL3
		auto decoder = std::make_unique<PushAulibDecoder>(audioInfo.nChannels, audioInfo.sampleRate);
		SVidAudioDecoder = decoder.get();
		SVidAudioStream.emplace(/*rwops=*/nullptr, std::move(decoder), CreateAulibResampler(audioInfo.sampleRate), /*closeRw=*/false);
		SVidAudioStream->setVolume(GetVolume());
#endif

#ifdef USE_SDL3
		SVidInitAudioStream(audioInfo);
#ifdef __PS3__
		LogInfo("PS3 FMV: SDL audio stream setup {}",
		    SVidAudioStream != nullptr ? "succeeded" : "unavailable");
#endif
#else
		if (!diablo_is_focused())
			SVidMute();
		if (!SVidAudioStream->open()) {
			LogError(LogCategory::Audio, "Aulib::Stream::open (from SVidPlayBegin): {}", SDL_GetError());
			SVidAudioStream = std::nullopt;
			SVidAudioDecoder = nullptr;
		} else if (!SVidAudioStream->play()) {
			LogError(LogCategory::Audio, "Aulib::Stream::play (from SVidPlayBegin): {}", SDL_GetError());
			SVidAudioStream = std::nullopt;
			SVidAudioDecoder = nullptr;
		} else {
#ifdef __PS3__
			LogInfo("PS3 FMV: audio stream opened and playing");
#endif
		}
#endif
	}
#endif

	// SMK format internally defines the frame rate as the frame duration
	// in either milliseconds or SMK time units (0.01ms). The library converts it
	// to FPS, which is always an integer, and here we convert it back to SMK time units.
	const float frameRate = SVidDecoderGetFrameRate();
	if (!(frameRate > 0.0F)) {
		LogError("PS3 FMV: decoder reported an invalid frame rate");
		SVidPlayEnd();
		return false;
	}
	SVidFrameLength = static_cast<uint32_t>(100000.0F / frameRate);
	if (SVidFrameLength == 0) {
		LogError("PS3 FMV: calculated zero-length frame duration");
		SVidPlayEnd();
		return false;
	}
	SVidDecoderGetFrameSize(SVidWidth, SVidHeight);
#ifdef __PS3__
	LogInfo("PS3 FMV: {}x{} {} fps, {} frames",
	    SVidWidth, SVidHeight, SVidDecoderGetFrameRate(), SVidDecoderGetNumFrames());
#endif

#ifndef USE_SDL1
	if (renderer != nullptr) {
		const int renderWidth = static_cast<int>(SVidWidth);
		const int renderHeight = static_cast<int>(SVidHeight);
#ifdef __PS3__
		LogInfo("PS3 FMV: streaming texture create begin {}x{} format=0x{:08x}",
		    renderWidth, renderHeight, static_cast<unsigned int>(DEVILUTIONX_DISPLAY_TEXTURE_FORMAT));
		SDLTextureUniquePtr replacementTexture {
		    SDL_CreateTexture(renderer, DEVILUTIONX_DISPLAY_TEXTURE_FORMAT,
		        SDL_TEXTUREACCESS_STREAMING, renderWidth, renderHeight)
		};
		if (replacementTexture == nullptr) {
			LogError("PS3 FMV: streaming texture creation failed: {}", SDL_GetError());
			ErrSdl();
			return false;
		}
		LogInfo("PS3 FMV: streaming texture allocation returned valid texture");
		LogInfo("PS3 FMV: replacing current renderer texture begin old_valid={}", texture != nullptr);
		texture.swap(replacementTexture);
		LogInfo("PS3 FMV: current renderer texture replaced; old texture cleanup begin");
		replacementTexture.reset();
		LogInfo("PS3 FMV: old texture cleanup complete");
#else
		texture = SDLWrap::CreateTexture(renderer, DEVILUTIONX_DISPLAY_TEXTURE_FORMAT, SDL_TEXTUREACCESS_STREAMING, renderWidth, renderHeight);
#endif
		if (
#ifdef USE_SDL3
		    !SDL_SetRenderLogicalPresentation(renderer, renderWidth, renderHeight,
		        *GetOptions().Graphics.integerScaling ? SDL_LOGICAL_PRESENTATION_INTEGER_SCALE : SDL_LOGICAL_PRESENTATION_LETTERBOX)
#else
		    SDL_RenderSetLogicalSize(renderer, renderWidth, renderHeight) <= -1
#endif
		) {
			ErrSdl();
		}
	}
#if defined(DEVILUTIONX_DISPLAY_PIXELFORMAT) && DEVILUTIONX_DISPLAY_PIXELFORMAT == SDL_PIXELFORMAT_INDEX8
	else {
		const Size windowSize = { static_cast<int>(SVidWidth), static_cast<int>(SVidHeight) };
		SDL_DisplayMode nearestDisplayMode = GetNearestDisplayMode(windowSize, DEVILUTIONX_DISPLAY_PIXELFORMAT);
#ifdef USE_SDL3
		if (!SDL_SetWindowFullscreenMode(ghMainWnd, &nearestDisplayMode)) {
#else
		if (SDL_SetWindowDisplayMode(ghMainWnd, &nearestDisplayMode) != 0) {
#endif
			ErrSdl();
		}
	}
#endif
#else
	TrySetVideoModeToSVidForSDL1();
#endif

	// Set the background to black.
	SDL_FillSurfaceRect(GetOutputSurface(), nullptr, 0x000000);

	// The buffer for the frame. It is not the same as the SDL surface because the SDL surface also has pitch padding.
	SVidFrameBuffer = std::unique_ptr<uint8_t[]> { new uint8_t[static_cast<size_t>(SVidWidth * SVidHeight)] };
#ifdef __PS3__
	LogInfo("PS3 FMV: indexed frame buffer allocated ({} bytes)",
	    static_cast<size_t>(SVidWidth) * static_cast<size_t>(SVidHeight));
#endif

	// Decode first frame.
	const uint32_t firstFrame = SVidDecoderGetNextFrame();
	if (firstFrame == std::numeric_limits<uint32_t>::max()) {
		LogError("PS3 FMV: decoder failed on the first frame of {}", filename);
		SVidPlayEnd();
		return false;
	}
	SVidDecoderGetFrame(SVidFrameBuffer.get());
#ifdef __PS3__
	LogInfo("PS3 FMV: first frame decoded");
#endif

	// Create the surface from the frame buffer data.
	// It will be rendered in `SVidPlayContinue`, called immediately after this function.
	// Subsequents frames will also be copied to this surface.
	SVidSurface = SDLWrap::CreateRGBSurfaceWithFormatFrom(
	    reinterpret_cast<void *>(SVidFrameBuffer.get()),
	    static_cast<int>(SVidWidth),
	    static_cast<int>(SVidHeight),
	    8,
	    static_cast<int>(SVidWidth),
	    SDL_PIXELFORMAT_INDEX8);

	SVidPalette = SDLWrap::AllocPalette();
	UpdatePalette();

	SVidFrameEnd = GetTicksSmk() + SVidFrameLength;
#ifdef __PS3__
	LogInfo("PS3 FMV: playback setup complete");
#endif

	return true;
}

bool SVidPlayContinue()
{
	const uint64_t frameDeadline = SVidFrameEnd;
#ifdef __PS3__
	const Uint32 continueStartTicks = SDL_GetTicks();
	const uint32_t frameNumber = SVidDecoderGetCurrentFrameNum();
	const uint32_t continueIndex = SVidDebugContinueCount++;
	// Keep detailed PS3 frame tracing enabled until the intro playback stall is isolated.
	const bool traceFrame = true;
	if (traceFrame)
		LogInfo("PS3 FMV TRACE: continue begin index={} frame={}", continueIndex, frameNumber);
	const uint64_t entryTime = GetTicksSmk();
	const int64_t deadlineOffsetMs = entryTime >= frameDeadline
	    ? static_cast<int64_t>(TimeSmkToMs(entryTime - frameDeadline))
	    : -static_cast<int64_t>(TimeSmkToMs(frameDeadline - entryTime));
	const Uint32 paletteStartTicks = traceFrame ? SDL_GetTicks() : 0;
#endif
#ifdef __PS3__
	if (traceFrame)
		LogInfo("PS3 FMV TRACE: palette check begin frame={}", frameNumber);
#endif
	const bool paletteChanged = SVidDecoderDidPaletteChange();
	if (paletteChanged)
		UpdatePalette();
#ifdef __PS3__
	if (traceFrame)
		LogInfo("PS3 FMV TRACE: palette check complete frame={} changed={}", frameNumber, paletteChanged);
#endif
#ifdef __PS3__
	const Uint32 paletteMs = traceFrame ? SDL_GetTicks() - paletteStartTicks : 0;
#endif

	if (GetTicksSmk() >= frameDeadline) {
#ifdef __PS3__
		const uint64_t lateByMs = TimeSmkToMs(GetTicksSmk() - frameDeadline);
#endif
#if defined(USE_SDL3) && !defined(NOSOUND)
		if (ShouldPushAudioData()) SDL_ClearAudioStream(SVidAudioStream);
#endif
#ifdef __PS3__
		const Uint32 decodeStartTicks = traceFrame ? SDL_GetTicks() : 0;
#endif
#ifdef __PS3__
		if (traceFrame)
			LogInfo("PS3 FMV TRACE: late-frame decode begin frame={} path=before-audio", frameNumber);
#endif
		const bool hasNextFrame = SVidLoadNextFrame();
#ifdef __PS3__
		if (traceFrame)
			LogInfo("PS3 FMV TRACE: late-frame decode complete frame={} path=before-audio next={}", frameNumber, hasNextFrame);
#endif
#ifdef __PS3__
		if (traceFrame) {
			const Uint32 decodeMs = SDL_GetTicks() - decodeStartTicks;
			LogInfo("PS3 FMV: frame {} skipped before audio; deadline_offset={} ms late={} ms palette={} ms decode_next={} ms total={} ms",
			    frameNumber, deadlineOffsetMs, lateByMs, paletteMs, decodeMs,
			    SDL_GetTicks() - continueStartTicks);
		}
#endif
		return hasNextFrame; // Skip video and audio if the system is too slow
	}

#ifdef __PS3__
	const Uint32 audioStartTicks = traceFrame ? SDL_GetTicks() : 0;
#endif
#ifndef NOSOUND
#ifdef __PS3__
	LogInfo("PS3 FMV TRACE: audio gate begin frame={}", frameNumber);
#endif
	const bool audioReady = ShouldPushAudioData();
#ifdef __PS3__
	LogInfo("PS3 FMV TRACE: audio gate complete frame={} ready={}", frameNumber, audioReady);
#endif
	if (audioReady) {
		std::int16_t *buf = SVidAudioBuffer.get();
#ifdef __PS3__
		if (traceFrame)
			LogInfo("PS3 FMV TRACE: audio decode begin frame={}", frameNumber);
#endif
		const auto len = SVidDecoderGetAudioData(0, buf);
#ifdef __PS3__
		if (traceFrame)
			LogInfo("PS3 FMV TRACE: audio decode complete frame={} bytes={}", frameNumber, len);
#endif
#ifdef USE_SDL3
		if (!SDL_PutAudioStreamData(SVidAudioStream, buf, static_cast<int>(len))) {
			LogError(LogCategory::Audio, "SDL_PutAudioStreamData (from SVidPlayContinue): {}", SDL_GetError());
			SDL_ClearError();
			SDL_DestroyAudioStream(SVidAudioStream);
			SVidAudioStream = nullptr;
		}
#else
		if (SVidAudioDepth == 16) {
#ifdef __PS3__
			if (traceFrame)
				LogInfo("PS3 FMV TRACE: audio queue push begin frame={} samples={}", frameNumber, len / 2);
#endif
			SVidAudioDecoder->PushSamples(buf, len / 2);
#ifdef __PS3__
			if (traceFrame)
				LogInfo("PS3 FMV TRACE: audio queue push complete frame={}", frameNumber);
#endif
		} else {
#ifdef __PS3__
			if (traceFrame)
				LogInfo("PS3 FMV TRACE: audio queue push begin frame={} bytes={}", frameNumber, len);
#endif
			SVidAudioDecoder->PushSamples(reinterpret_cast<const std::uint8_t *>(buf), len);
#ifdef __PS3__
			if (traceFrame)
				LogInfo("PS3 FMV TRACE: audio queue push complete frame={}", frameNumber);
#endif
		}
#endif
	}
#endif
#ifdef __PS3__
	const Uint32 audioMs = traceFrame ? SDL_GetTicks() - audioStartTicks : 0;
#endif

	if (GetTicksSmk() >= frameDeadline) {
#ifdef __PS3__
		const uint64_t lateByMs = TimeSmkToMs(GetTicksSmk() - frameDeadline);
		const Uint32 decodeStartTicks = traceFrame ? SDL_GetTicks() : 0;
#endif
#ifdef __PS3__
		if (traceFrame)
			LogInfo("PS3 FMV TRACE: late-frame decode begin frame={} path=after-audio", frameNumber);
#endif
		const bool hasNextFrame = SVidLoadNextFrame();
#ifdef __PS3__
		if (traceFrame)
			LogInfo("PS3 FMV TRACE: late-frame decode complete frame={} path=after-audio next={}", frameNumber, hasNextFrame);
#endif
#ifdef __PS3__
		if (traceFrame) {
			const Uint32 decodeMs = SDL_GetTicks() - decodeStartTicks;
			LogInfo("PS3 FMV: frame {} skipped after audio; deadline_offset={} ms late={} ms palette={} ms audio={} ms decode_next={} ms total={} ms",
			    frameNumber, deadlineOffsetMs, lateByMs, paletteMs, audioMs, decodeMs,
			    SDL_GetTicks() - continueStartTicks);
		}
#endif
		return hasNextFrame; // Skip video when the system is too slow
	}

#ifdef __PS3__
	PS3FMVRenderStats renderStats {};
	const Uint32 presentationStartTicks = traceFrame ? SDL_GetTicks() : 0;
	if (traceFrame)
		LogInfo("PS3 FMV TRACE: renderer begin frame={}", frameNumber);
	if (!BlitFrame(traceFrame ? &renderStats : nullptr)) {
		if (traceFrame)
			LogInfo("PS3 FMV TRACE: renderer failed frame={}", frameNumber);
		return false;
	}
	if (traceFrame)
		LogInfo("PS3 FMV TRACE: renderer complete frame={}", frameNumber);
	const Uint32 presentationMs = traceFrame ? SDL_GetTicks() - presentationStartTicks : 0;
#else
	if (!BlitFrame(false))
		return false;
#endif

	const uint64_t now = GetTicksSmk();
#ifdef __PS3__
	Uint32 pacingWaitMs = 0;
#endif
	if (now < frameDeadline) {
		const Uint32 delayMs = static_cast<Uint32>(TimeSmkToMs(frameDeadline - now));
#ifdef __PS3__
		const Uint32 waitStartTicks = traceFrame ? SDL_GetTicks() : 0;
#endif
#ifdef __PS3__
		if (traceFrame)
			LogInfo("PS3 FMV TRACE: pacing delay begin frame={} ms={}", frameNumber, delayMs);
#endif
		SDL_Delay(delayMs); // wait with next frame if the system is too fast
#ifdef __PS3__
		if (traceFrame)
			LogInfo("PS3 FMV TRACE: pacing delay complete frame={}", frameNumber);
#endif
#ifdef __PS3__
		if (traceFrame)
			pacingWaitMs = SDL_GetTicks() - waitStartTicks;
#endif
	}

#ifdef __PS3__
	const Uint32 decodeStartTicks = traceFrame ? SDL_GetTicks() : 0;
#endif
#ifdef __PS3__
	if (traceFrame)
		LogInfo("PS3 FMV TRACE: next-frame decode begin after frame={}", frameNumber);
#endif
	const bool hasNextFrame = SVidLoadNextFrame();
#ifdef __PS3__
	if (traceFrame)
		LogInfo("PS3 FMV TRACE: next-frame decode complete after frame={} next={}", frameNumber, hasNextFrame);
#endif
#ifdef __PS3__
	if (traceFrame) {
		const Uint32 decodeMs = SDL_GetTicks() - decodeStartTicks;
		const uint64_t afterPresentTime = GetTicksSmk();
		const int64_t lateAfterPresentMs = afterPresentTime >= frameDeadline
		    ? static_cast<int64_t>(TimeSmkToMs(afterPresentTime - frameDeadline))
		    : -static_cast<int64_t>(TimeSmkToMs(frameDeadline - afterPresentTime));
		LogInfo("PS3 FMV: frame {} timing: deadline_offset={} ms late_after={} ms palette={} ms audio={} ms presentation={} ms [setup={} lock={} expand={} copy={} present={}] wait={} ms decode_next={} ms total={} ms next={}",
		    frameNumber, deadlineOffsetMs, lateAfterPresentMs, paletteMs, audioMs, presentationMs,
		    renderStats.textureSetupMs, renderStats.textureLockMs, renderStats.pixelExpandMs,
		    renderStats.renderCopyMs, renderStats.renderPresentMs, pacingWaitMs, decodeMs,
		    SDL_GetTicks() - continueStartTicks, hasNextFrame ? "ready" : "end");
	}
#endif
	return hasNextFrame;
}

void SVidPlayEnd()
{
#ifdef __PS3__
	LogInfo("PS3 FMV END: begin handle_valid={}", SVidHandle.isValid);
#endif
#ifndef NOSOUND
#ifdef __PS3__
	LogInfo("PS3 FMV END: audio stream shutdown begin active={}", static_cast<bool>(SVidAudioStream));
#endif
	if (SVidAudioStream) {
#ifdef USE_SDL3
		SDL_DestroyAudioStream(SVidAudioStream);
		SVidAudioStream = nullptr;
		if (SVidAudioDevice != 0) {
			SDL_CloseAudioDevice(SVidAudioDevice);
			SVidAudioDevice = 0;
		}
#else
#ifdef __PS3__
		if (SVidAudioDecoder != nullptr) {
			LogInfo("PS3 FMV audio queue: high_water={} samples limit={} samples dropped={} samples",
			    SVidAudioDecoder->QueueHighWaterSamples(),
			    SVidAudioDecoder->QueueLimitSamples(),
			    SVidAudioDecoder->QueueDroppedSamples());
		}
#endif
		SVidAudioStream = std::nullopt;
		SVidAudioDecoder = nullptr;
#endif
	}
	SVidAudioBuffer = nullptr;
#ifdef __PS3__
	LogInfo("PS3 FMV END: audio stream shutdown complete");
#endif
#endif

#ifdef __PS3__
	LogInfo("PS3 FMV END: decoder close begin valid={}", SVidDecoderIsValid());
#endif
	if (SVidDecoderIsValid())
		SVidDecoderClose();
#ifdef __PS3__
	LogInfo("PS3 FMV END: decoder close complete valid={}", SVidDecoderIsValid());
#endif

#ifdef __PS3__
	LogInfo("PS3 FMV END: release palette/surface/framebuffer begin");
#endif
	SVidPalette = nullptr;
	SVidSurface = nullptr;
	SVidFrameBuffer = nullptr;
#ifdef __PS3__
	LogInfo("PS3 FMV END: release palette/surface/framebuffer complete");
#endif

#ifndef USE_SDL1
	if (renderer != nullptr) {
#ifdef __PS3__
		LogInfo("PS3 FMV END: restore game texture begin {}x{}", gnScreenWidth, gnScreenHeight);
#endif
#ifdef __PS3__
		const Uint32 restoreTextureStartTicks = SDL_GetTicks();
#endif
		texture = SDLWrap::CreateTexture(renderer, DEVILUTIONX_DISPLAY_TEXTURE_FORMAT, SDL_TEXTUREACCESS_STREAMING, gnScreenWidth, gnScreenHeight);
#ifdef __PS3__
		LogInfo("PS3 FMV END: restore game texture complete valid={} elapsed_ms={}",
		    texture != nullptr, SDL_GetTicks() - restoreTextureStartTicks);
#endif
		if (
#ifdef USE_SDL3
		    !SDL_SetRenderLogicalPresentation(renderer, gnScreenWidth, gnScreenHeight,
		        *GetOptions().Graphics.integerScaling ? SDL_LOGICAL_PRESENTATION_INTEGER_SCALE : SDL_LOGICAL_PRESENTATION_LETTERBOX)
#else
		    SDL_RenderSetLogicalSize(renderer, gnScreenWidth, gnScreenHeight) <= -1
#endif
		) {
			ErrSdl();
		}
	}
#if defined(DEVILUTIONX_DISPLAY_PIXELFORMAT) && DEVILUTIONX_DISPLAY_PIXELFORMAT == SDL_PIXELFORMAT_INDEX8
	else {
		const Size windowSize = { static_cast<int>(gnScreenWidth), static_cast<int>(gnScreenHeight) };
		SDL_DisplayMode nearestDisplayMode = GetNearestDisplayMode(windowSize, DEVILUTIONX_DISPLAY_PIXELFORMAT);
#ifdef USE_SDL3
		if (!SDL_SetWindowFullscreenMode(ghMainWnd, &nearestDisplayMode)) {
#else
		if (SDL_SetWindowDisplayMode(ghMainWnd, &nearestDisplayMode) != 0) {
#endif
			ErrSdl();
		}
	}
#endif
#else
	if (IsSVidVideoMode) {
		SetVideoModeToPrimary(IsFullScreen(), gnScreenWidth, gnScreenHeight);
		IsSVidVideoMode = false;
	}
#endif
#ifdef __PS3__
	LogInfo("PS3 FMV END: complete");
#endif
}

void SVidMute()
{
#ifndef NOSOUND
#ifdef USE_SDL3
	SVidAutoStreamEnabled = false;
#else
	if (SVidAudioStream)
		SVidAudioStream->mute();
#endif
#endif
}

void SVidUnmute()
{
#ifndef NOSOUND
#ifdef USE_SDL3
	if (SVidAudioStream != nullptr) SVidAutoStreamEnabled = true;
#else
	if (SVidAudioStream)
		SVidAudioStream->unmute();
#endif
#endif
}

} // namespace devilution
