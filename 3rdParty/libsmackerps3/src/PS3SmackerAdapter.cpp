#include "PS3Smacker.h"

#include "smacker.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace devilution {
namespace {

#ifndef DEVILUTIONX_PS3_TITLE_ID
#define DEVILUTIONX_PS3_TITLE_ID "DEVILUT00"
#endif

constexpr size_t MaxMovieBytes = 128U * 1024U * 1024U;
constexpr size_t CopyBufferSize = 32U * 1024U;
constexpr char TempMovieName[] = ".devilutionx-smacker-fmv.tmp";

struct SmackerInstance {
	smk decoder = nullptr;
	std::string tempPath;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t frameCount = 0;
	uint32_t nextFrame = 0;
	double frameDurationUs = 0.0;
	uint8_t trackMask = 0;
	uint8_t channels[7] {};
	uint8_t bitsPerSample[7] {};
	unsigned long audioRate[7] {};
	unsigned long audioMaxSize[7] {};
	unsigned char audioCompression[7] {};
	bool firstFramePending = true;
	bool decodeFailed = false;
	bool paletteInitialized = false;
	bool paletteChanged = false;
	uint8_t previousPalette[768] {};
};

std::vector<std::unique_ptr<SmackerInstance>> Instances;

std::string GetTempMoviePath()
{
	return std::string("/dev_hdd0/game/") + DEVILUTIONX_PS3_TITLE_ID
	    + "/USRDIR/" + TempMovieName;
}

SmackerInstance *GetInstance(PS3SmackerHandle &handle)
{
	if (!handle.isValid || handle.instanceIndex < 0
	    || static_cast<size_t>(handle.instanceIndex) >= Instances.size())
		return nullptr;
	return Instances[static_cast<size_t>(handle.instanceIndex)].get();
}

void CloseInstance(SmackerInstance &instance)
{
	if (instance.decoder != nullptr) {
		smk_close(instance.decoder);
		instance.decoder = nullptr;
	}
	if (!instance.tempPath.empty()) {
		std::remove(instance.tempPath.c_str());
		instance.tempPath.clear();
	}
}

bool CopySdlStreamToDisk(
#ifdef USE_SDL3
    SDL_IOStream *stream,
#else
    SDL_RWops *stream,
#endif
    const std::string &path)
{
	if (stream == nullptr)
		return false;

	FILE *output = std::fopen(path.c_str(), "wb");
	if (output == nullptr) {
		std::fprintf(stderr, "[PS3Smacker] Cannot create temporary movie file: %s\n", path.c_str());
#ifdef USE_SDL3
		SDL_CloseIO(stream);
#else
		SDL_RWclose(stream);
#endif
		return false;
	}

	std::array<uint8_t, CopyBufferSize> buffer {};
	size_t totalBytes = 0;
	bool success = true;
#ifdef USE_SDL3
	const Sint64 expectedSize = SDL_GetIOSize(stream);
#else
	const Sint64 expectedSize = SDL_RWsize(stream);
#endif
	if (expectedSize > static_cast<Sint64>(MaxMovieBytes)) {
		std::fprintf(stderr, "[PS3Smacker] Refusing oversized movie (%lld bytes)\n",
		    static_cast<long long>(expectedSize));
		success = false;
	}

	while (success && (expectedSize < 0 || totalBytes < static_cast<size_t>(expectedSize))) {
		size_t request = buffer.size();
		if (expectedSize >= 0)
			request = std::min(request, static_cast<size_t>(expectedSize) - totalBytes);
#ifdef USE_SDL3
		const size_t bytesRead = SDL_ReadIO(stream, buffer.data(), request);
#else
		const size_t bytesRead = SDL_RWread(stream, buffer.data(), 1, request);
#endif
		if (bytesRead == 0) {
			if (expectedSize >= 0 && totalBytes != static_cast<size_t>(expectedSize))
				success = false;
			break;
		}
		if (totalBytes > MaxMovieBytes || bytesRead > MaxMovieBytes - totalBytes
		    || std::fwrite(buffer.data(), 1, bytesRead, output) != bytesRead) {
			success = false;
			break;
		}
		totalBytes += bytesRead;
	}

#ifdef USE_SDL3
	if (!SDL_CloseIO(stream))
		success = false;
#else
	if (SDL_RWclose(stream) != 0)
		success = false;
#endif
	if (std::fflush(output) != 0)
		success = false;
	if (std::fclose(output) != 0)
		success = false;
	if (totalBytes == 0)
		success = false;

	if (!success)
		std::remove(path.c_str());
	return success;
}

void UpdatePaletteState(SmackerInstance &instance)
{
	const unsigned char *palette = smk_get_palette(instance.decoder);
	if (palette == nullptr)
		return;
	instance.paletteChanged = !instance.paletteInitialized
	    || std::memcmp(instance.previousPalette, palette, sizeof(instance.previousPalette)) != 0;
	std::memcpy(instance.previousPalette, palette, sizeof(instance.previousPalette));
	instance.paletteInitialized = true;
}

} // namespace

#ifdef USE_SDL3
PS3SmackerHandle PS3Smacker_Open(SDL_IOStream *stream)
#else
PS3SmackerHandle PS3Smacker_Open(SDL_RWops *stream)
#endif
{
	PS3SmackerHandle handle {};
	auto instance = std::make_unique<SmackerInstance>();
	instance->tempPath = GetTempMoviePath();
	std::remove(instance->tempPath.c_str());

	if (!CopySdlStreamToDisk(stream, instance->tempPath)) {
		std::fprintf(stderr, "[PS3Smacker] Failed while extracting SMK stream to temporary disk file\n");
		return handle;
	}

	FILE *input = std::fopen(instance->tempPath.c_str(), "rb");
	if (input == nullptr) {
		std::fprintf(stderr, "[PS3Smacker] Cannot reopen temporary movie file\n");
		std::remove(instance->tempPath.c_str());
		return handle;
	}

	instance->decoder = smk_open_filepointer(input, SMK_MODE_DISK);
	if (instance->decoder == nullptr) {
		std::fprintf(stderr, "[PS3Smacker] libsmacker failed to parse the movie header\n");
		std::remove(instance->tempPath.c_str());
		return handle;
	}

	unsigned long width = 0;
	unsigned long height = 0;
	unsigned long frameCount = 0;
	if (smk_info_video(instance->decoder, &width, &height, nullptr) != 0
	    || smk_info_all(instance->decoder, nullptr, &frameCount, &instance->frameDurationUs) != 0
	    || width == 0 || height == 0 || frameCount == 0
	    || width > 1920 || height > 1080
	    || width > std::numeric_limits<uint32_t>::max()
	    || height > std::numeric_limits<uint32_t>::max()
	    || frameCount > 100000
	    || width > std::numeric_limits<size_t>::max() / height) {
		std::fprintf(stderr, "[PS3Smacker] Invalid video metadata in SMK file\n");
		CloseInstance(*instance);
		return handle;
	}

	instance->width = static_cast<uint32_t>(width);
	instance->height = static_cast<uint32_t>(height);
	instance->frameCount = static_cast<uint32_t>(frameCount);
	if (smk_info_audio(instance->decoder, &instance->trackMask, instance->channels,
	        instance->bitsPerSample, instance->audioRate) != 0) {
		std::fprintf(stderr, "[PS3Smacker] Failed to read audio track metadata\n");
		CloseInstance(*instance);
		return handle;
	}
	uint8_t enabledAudioMask = 0;
	for (uint32_t i = 0; i < 7; ++i) {
		instance->audioMaxSize[i] = smk_get_audio_max_size(instance->decoder, static_cast<unsigned char>(i));
		instance->audioCompression[i] = smk_get_audio_compression(instance->decoder, static_cast<unsigned char>(i));
		if ((instance->trackMask & (1U << i)) != 0 && instance->audioCompression[i] != 2) {
			enabledAudioMask |= static_cast<uint8_t>(1U << i);
		} else {
			instance->channels[i] = 0;
			instance->bitsPerSample[i] = 0;
			instance->audioRate[i] = 0;
		}
	}
	instance->trackMask = enabledAudioMask;

	const unsigned char enableMask = static_cast<unsigned char>(SMK_VIDEO_TRACK | enabledAudioMask);
	if (smk_enable_all(instance->decoder, enableMask) != 0) {
		std::fprintf(stderr, "[PS3Smacker] Failed to enable SMK video/audio streams\n");
		CloseInstance(*instance);
		return handle;
	}

	int index = -1;
	for (size_t i = 0; i < Instances.size(); ++i) {
		if (Instances[i] == nullptr) {
			index = static_cast<int>(i);
			Instances[i] = std::move(instance);
			break;
		}
	}
	if (index < 0) {
		index = static_cast<int>(Instances.size());
		Instances.push_back(std::move(instance));
	}
	handle.instanceIndex = index;
	handle.isValid = true;
	std::fprintf(stderr, "[PS3Smacker] Opened %ux%u, %u frames, %.3f fps using HDD-streamed C decoder\n",
	    Instances[static_cast<size_t>(index)]->width,
	    Instances[static_cast<size_t>(index)]->height,
	    Instances[static_cast<size_t>(index)]->frameCount,
	    Instances[static_cast<size_t>(index)]->frameDurationUs > 0.0
	        ? 1000000.0 / Instances[static_cast<size_t>(index)]->frameDurationUs
	        : 0.0);
	return handle;
}

void PS3Smacker_Close(PS3SmackerHandle &handle)
{
	SmackerInstance *instance = GetInstance(handle);
	if (instance != nullptr) {
		CloseInstance(*instance);
		Instances[static_cast<size_t>(handle.instanceIndex)].reset();
	}
	handle.isValid = false;
	handle.instanceIndex = -1;
}

uint32_t PS3Smacker_GetNumAudioTracks(PS3SmackerHandle &handle)
{
	SmackerInstance *instance = GetInstance(handle);
	if (instance == nullptr)
		return 0;
	uint32_t count = 0;
	for (uint32_t i = 0; i < 7; ++i)
		count += (instance->trackMask & (1U << i)) != 0;
	return count;
}

SmackerAudioInfo PS3Smacker_GetAudioTrackDetails(PS3SmackerHandle &handle, uint32_t trackIndex)
{
	SmackerAudioInfo info {};
	SmackerInstance *instance = GetInstance(handle);
	if (instance == nullptr || trackIndex >= 7)
		return info;
	info.sampleRate = static_cast<uint32_t>(instance->audioRate[trackIndex]);
	info.nChannels = instance->channels[trackIndex];
	info.bitsPerSample = instance->bitsPerSample[trackIndex];
	info.idealBufferSize = static_cast<uint32_t>(std::min<unsigned long>(
	    instance->audioMaxSize[trackIndex], std::numeric_limits<uint32_t>::max()));
	return info;
}

uint32_t PS3Smacker_GetAudioData(PS3SmackerHandle &handle, uint32_t trackIndex, int16_t *data)
{
	SmackerInstance *instance = GetInstance(handle);
	if (instance == nullptr || trackIndex >= 7 || data == nullptr)
		return 0;
	const unsigned char *source = smk_get_audio(instance->decoder, static_cast<unsigned char>(trackIndex));
	const unsigned long byteCount = smk_get_audio_size(instance->decoder, static_cast<unsigned char>(trackIndex));
	if (source == nullptr || byteCount == 0)
		return 0;
	if (byteCount > instance->audioMaxSize[trackIndex]) {
		instance->decodeFailed = true;
		std::fprintf(stderr, "[PS3Smacker] Audio track %u exceeded its header-declared buffer (%lu > %lu)\n",
		    trackIndex, byteCount, instance->audioMaxSize[trackIndex]);
		return 0;
	}
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	if (instance->bitsPerSample[trackIndex] == 16 && instance->audioCompression[trackIndex] == 0) {
		uint8_t *destination = reinterpret_cast<uint8_t *>(data);
		for (unsigned long i = 0; i + 1 < byteCount; i += 2) {
			destination[i] = source[i + 1];
			destination[i + 1] = source[i];
		}
		return static_cast<uint32_t>(byteCount);
	}
#endif
	std::memcpy(data, source, static_cast<size_t>(byteCount));
	return static_cast<uint32_t>(byteCount);
}

uint32_t PS3Smacker_GetNumFrames(PS3SmackerHandle &handle)
{
	SmackerInstance *instance = GetInstance(handle);
	return instance != nullptr ? instance->frameCount : 0;
}

void PS3Smacker_GetFrameSize(PS3SmackerHandle &handle, uint32_t &width, uint32_t &height)
{
	SmackerInstance *instance = GetInstance(handle);
	width = instance != nullptr ? instance->width : 0;
	height = instance != nullptr ? instance->height : 0;
}

uint32_t PS3Smacker_GetCurrentFrameNum(PS3SmackerHandle &handle)
{
	SmackerInstance *instance = GetInstance(handle);
	return instance != nullptr ? instance->nextFrame : 0;
}

uint32_t PS3Smacker_GetNextFrame(PS3SmackerHandle &handle)
{
	SmackerInstance *instance = GetInstance(handle);
	if (instance == nullptr || instance->decodeFailed || instance->nextFrame >= instance->frameCount)
		return std::numeric_limits<uint32_t>::max();
	const uint32_t decodedIndex = instance->nextFrame;
	const char result = instance->firstFramePending
	    ? smk_first(instance->decoder)
	    : smk_next(instance->decoder);
	instance->firstFramePending = false;
	if (result == SMK_ERROR || result == SMK_DONE) {
		instance->decodeFailed = true;
		std::fprintf(stderr, "[PS3Smacker] Frame decode failed at index %u\n", decodedIndex);
		return std::numeric_limits<uint32_t>::max();
	}
	++instance->nextFrame;
	UpdatePaletteState(*instance);
	return decodedIndex;
}

float PS3Smacker_GetFrameRate(PS3SmackerHandle &handle)
{
	SmackerInstance *instance = GetInstance(handle);
	if (instance == nullptr || instance->frameDurationUs <= 0.0)
		return 0.0F;
	return static_cast<float>(1000000.0 / instance->frameDurationUs);
}

bool PS3Smacker_DidPaletteChange(PS3SmackerHandle &handle)
{
	SmackerInstance *instance = GetInstance(handle);
	return instance != nullptr && instance->paletteChanged;
}

void PS3Smacker_GetPalette(PS3SmackerHandle &handle, uint8_t *palette)
{
	SmackerInstance *instance = GetInstance(handle);
	if (instance == nullptr || palette == nullptr)
		return;
	const unsigned char *source = smk_get_palette(instance->decoder);
	if (source != nullptr)
		std::memcpy(palette, source, 768);
}

void PS3Smacker_GetFrame(PS3SmackerHandle &handle, uint8_t *frame)
{
	SmackerInstance *instance = GetInstance(handle);
	if (instance == nullptr || frame == nullptr)
		return;
	const unsigned char *source = smk_get_video(instance->decoder);
	if (source != nullptr)
		std::memcpy(frame, source, static_cast<size_t>(instance->width) * instance->height);
}

void PS3Smacker_Rewind(PS3SmackerHandle &handle)
{
	SmackerInstance *instance = GetInstance(handle);
	if (instance == nullptr)
		return;
	instance->nextFrame = 0;
	instance->firstFramePending = true;
	instance->decodeFailed = false;
	instance->paletteInitialized = false;
	instance->paletteChanged = false;
}

} // namespace devilution
