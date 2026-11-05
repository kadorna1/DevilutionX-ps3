#include "storm/ps3_vdec.h"

#ifdef __PS3__

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <stdlib.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL.h>
#include <codec/vdec.h>
#include <ppu-asm.h>
#include <sys/memory.h>
#include <sysmodule/sysmodule.h>

#ifndef NOSOUND
#include "utils/aulib.hpp"
#include "utils/push_aulib_decoder.h"
#endif

#include "engine/assets.hpp"
#include "engine/dx.h"
#include "engine/sound.h"
#include "options.h"
#include "utils/display.h"
#include "utils/log.hpp"
#include "utils/sdl_wrap.h"

namespace devilution {
namespace {

constexpr std::string_view MetadataMagic = "DVLX-PS3-VDEC-1";
constexpr Uint32 VdecTimeoutMs = 5000;
constexpr Uint32 VdecPpuThreadPriority = 1400;
constexpr Uint32 VdecSpuThreadPriority = 100;
constexpr Uint32 VdecSpuCount = 4;
constexpr size_t VdecMemoryPageSize = 1024 * 1024;
constexpr size_t VdecPacketAlignment = 128;
constexpr size_t VdecOutputAlignment = 128;

// Allocate extra space and retain the original malloc pointer so the aligned
// address can still be released safely with free().
void *AllocateAlignedBuffer(size_t size, size_t alignment, void *&allocation)
{
	if (size == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0)
		return nullptr;
	if (size > std::numeric_limits<size_t>::max() - (alignment - 1))
		return nullptr;

	void *base = std::malloc(size + alignment - 1);
	if (base == nullptr)
		return nullptr;
	const uintptr_t address = reinterpret_cast<uintptr_t>(base);
	const uintptr_t alignedAddress = (address + alignment - 1) & ~(static_cast<uintptr_t>(alignment) - 1);
	allocation = base;
	return reinterpret_cast<void *>(alignedAddress);
}

struct AccessUnit {
	size_t offset = 0;
	size_t size = 0;
};

struct MovieMetadata {
	std::string videoPath;
	std::string audioPath;
	int width = 0;
	int height = 0;
	int fpsNumerator = 0;
	int fpsDenominator = 0;
	int frameCount = 0;
	int audioRate = 0;
	int audioChannels = 0;
};

struct VdecMovieState {
	MovieMetadata metadata;
	std::vector<AccessUnit> accessUnits;
	Uint8 *videoData = nullptr;
	void *videoDataAllocation = nullptr;
	size_t videoSize = 0;
	std::unique_ptr<char[]> audioData;
	size_t audioSize = 0;
	Uint8 *decodedFrame = nullptr;
	void *decodedFrameAllocation = nullptr;
	size_t decodedFrameSize = 0;
	int codedWidth = 0;
	int codedHeight = 0;
	size_t frameIndex = 0;
	Uint32 startTicks = 0;

	SDL_mutex *mutex = nullptr;
	bool accessUnitDone = false;
	bool frameReady = false;
	bool sequenceDone = false;
	int callbackError = 0;

	u32 decoderHandle = 0;
	sys_mem_addr_t decoderMemory = 0;
	bool decoderOpened = false;
	bool sequenceStarted = false;
	bool textureReplaced = false;
	bool loop = false;

#ifndef NOSOUND
	std::optional<Aulib::Stream> audioStream;
	PushAulibDecoder *audioDecoder = nullptr;
#endif
};

std::unique_ptr<VdecMovieState> ActiveVdecMovie;

bool ParsePositiveInteger(std::string_view value, int &result)
{
	if (value.empty())
		return false;
	int parsed = 0;
	for (const char ch : value) {
		if (ch < '0' || ch > '9')
			return false;
		if (parsed > (std::numeric_limits<int>::max() - (ch - '0')) / 10)
			return false;
		parsed = parsed * 10 + (ch - '0');
	}
	if (parsed <= 0)
		return false;
	result = parsed;
	return true;
}

bool IsSafeRelativeAssetPath(std::string_view path)
{
	if (path.empty() || path.front() == '/' || path.front() == '\\')
		return false;
	if (path.find(':') != std::string_view::npos)
		return false;
	size_t start = 0;
	while (start <= path.size()) {
		const size_t end = path.find_first_of("/\\", start);
		const std::string_view component = path.substr(start, end == std::string_view::npos ? path.size() - start : end - start);
		if (component.empty() || component == "." || component == "..")
			return false;
		if (end == std::string_view::npos)
			break;
		start = end + 1;
	}
	return true;
}

bool ParseMetadata(std::string_view contents, MovieMetadata &metadata)
{
	const size_t firstNewline = contents.find('\n');
	if (contents.substr(0, firstNewline) != MetadataMagic)
		return false;
	contents = firstNewline == std::string_view::npos ? std::string_view {} : contents.substr(firstNewline + 1);

	bool hasWidth = false;
	bool hasHeight = false;
	bool hasFpsNumerator = false;
	bool hasFpsDenominator = false;
	bool hasFrameCount = false;
	bool hasVideo = false;
	bool hasPixelFormat = false;
	bool hasBitstream = false;
	bool hasAudioFormat = false;

	while (!contents.empty()) {
		const size_t newline = contents.find('\n');
		std::string_view line = contents.substr(0, newline);
		contents = newline == std::string_view::npos ? std::string_view {} : contents.substr(newline + 1);
		if (!line.empty() && line.back() == '\r')
			line.remove_suffix(1);
		const size_t separator = line.find('=');
		if (separator == std::string_view::npos)
			continue;
		const std::string_view key = line.substr(0, separator);
		const std::string_view value = line.substr(separator + 1);
		int parsed = 0;
		if (key == "width") {
			if (!ParsePositiveInteger(value, parsed))
				return false;
			metadata.width = parsed;
			hasWidth = true;
		} else if (key == "height") {
			if (!ParsePositiveInteger(value, parsed))
				return false;
			metadata.height = parsed;
			hasHeight = true;
		} else if (key == "fps_num") {
			if (!ParsePositiveInteger(value, parsed))
				return false;
			metadata.fpsNumerator = parsed;
			hasFpsNumerator = true;
		} else if (key == "fps_den") {
			if (!ParsePositiveInteger(value, parsed))
				return false;
			metadata.fpsDenominator = parsed;
			hasFpsDenominator = true;
		} else if (key == "frame_count") {
			if (!ParsePositiveInteger(value, parsed))
				return false;
			metadata.frameCount = parsed;
			hasFrameCount = true;
		} else if (key == "audio_rate") {
			if (value != "0" && !ParsePositiveInteger(value, parsed))
				return false;
			metadata.audioRate = value == "0" ? 0 : parsed;
		} else if (key == "audio_channels") {
			if (value != "0" && !ParsePositiveInteger(value, parsed))
				return false;
			metadata.audioChannels = value == "0" ? 0 : parsed;
		} else if (key == "video") {
			metadata.videoPath.assign(value);
			hasVideo = true;
		} else if (key == "audio") {
			metadata.audioPath.assign(value);
		} else if (key == "pixel_format") {
			hasPixelFormat = value == "yuv420p";
		} else if (key == "bitstream") {
			hasBitstream = value == "annexb-aud";
		} else if (key == "audio_format") {
			hasAudioFormat = value == "s16be";
		}
	}

	if (!hasWidth || !hasHeight || !hasFpsNumerator || !hasFpsDenominator || !hasFrameCount || !hasVideo || !hasPixelFormat || !hasBitstream || !hasAudioFormat)
		return false;
	if (metadata.width > 1920 || metadata.height > 1080 || metadata.frameCount > 100000)
		return false;
	if (metadata.fpsNumerator > 120000 || metadata.fpsDenominator > 10000)
		return false;
	if (metadata.audioRate > 48000 || metadata.audioChannels > 2)
		return false;
	if (!IsSafeRelativeAssetPath(metadata.videoPath))
		return false;
	if (!metadata.audioPath.empty() && metadata.audioPath != "none" && !IsSafeRelativeAssetPath(metadata.audioPath))
		return false;
	if (metadata.audioPath == "none")
		metadata.audioPath.clear();
	if (metadata.audioRate == 0 || metadata.audioChannels == 0)
		metadata.audioPath.clear();
	return true;
}

std::string MetadataPathForMovie(const char *filename)
{
	std::string path(filename != nullptr ? filename : "");
	const size_t separator = path.find_last_of("/\\");
	const size_t extension = path.find_last_of('.');
	if (extension == std::string::npos || (separator != std::string::npos && extension < separator))
		return {};
	path.resize(extension);
	path += ".ps3.meta";
	return path;
}

bool FindAnnexBStartCode(const Uint8 *data, size_t size, size_t from, size_t &prefixOffset, size_t &nalOffset)
{
	for (size_t i = from; i + 3 <= size; ++i) {
		if (i + 4 <= size && data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 0 && data[i + 3] == 1) {
			prefixOffset = i;
			nalOffset = i + 4;
			return true;
		}
		if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
			prefixOffset = i;
			nalOffset = i + 3;
			return true;
		}
	}
	return false;
}

bool BuildAccessUnits(const Uint8 *data, size_t size, size_t expectedFrames, std::vector<AccessUnit> &units)
{
	if (data == nullptr || size < 5)
		return false;
	std::vector<size_t> accessUnitStarts;
	size_t searchFrom = 0;
	size_t prefixOffset = 0;
	size_t nalOffset = 0;
	while (FindAnnexBStartCode(data, size, searchFrom, prefixOffset, nalOffset)) {
		if (nalOffset >= size)
			break;
		if ((data[nalOffset] & 0x1F) == 9)
			accessUnitStarts.push_back(prefixOffset);
		searchFrom = nalOffset + 1;
	}
	if (accessUnitStarts.empty())
		return false;
	// Include any leading SPS/PPS bytes in the first AU. x264 normally writes
	// the first AUD first, but accepting a leading prefix is harmless.
	if (accessUnitStarts.front() != 0)
		accessUnitStarts.front() = 0;
	if (accessUnitStarts.size() != expectedFrames)
		return false;

	units.clear();
	units.reserve(accessUnitStarts.size());
	for (size_t i = 0; i < accessUnitStarts.size(); ++i) {
		const size_t start = accessUnitStarts[i];
		const size_t end = i + 1 < accessUnitStarts.size() ? accessUnitStarts[i + 1] : size;
		if (end <= start || end - start > std::numeric_limits<u32>::max())
			return false;
		units.push_back(AccessUnit { start, end - start });
	}
	return true;
}

bool LoadMovieAssets(const char *filename, VdecMovieState &state)
{
	const std::string metadataPath = MetadataPathForMovie(filename);
	if (metadataPath.empty())
		return false;
	auto metadataAsset = LoadAsset(metadataPath);
	if (!metadataAsset.has_value()) {
		LogInfo("PS3 VDEC: no converted sidecar metadata for {}; using Smacker fallback", filename);
		return false;
	}
	if (!ParseMetadata(std::string_view(metadataAsset->data.get(), metadataAsset->size), state.metadata)) {
		LogError("PS3 VDEC: invalid conversion metadata in {}", metadataPath);
		return false;
	}

	auto videoAsset = LoadAsset(state.metadata.videoPath);
	if (!videoAsset.has_value() || videoAsset->size == 0) {
		LogError("PS3 VDEC: cannot load converted video {} ({})", state.metadata.videoPath,
		    videoAsset.has_value() ? "empty file" : videoAsset.error());
		return false;
	}
	void *alignedVideo = AllocateAlignedBuffer(videoAsset->size, VdecPacketAlignment, state.videoDataAllocation);
	if (alignedVideo == nullptr) {
		LogError("PS3 VDEC: cannot allocate {} bytes for H.264 stream", videoAsset->size);
		return false;
	}
	std::memcpy(alignedVideo, videoAsset->data.get(), videoAsset->size);
	state.videoData = static_cast<Uint8 *>(alignedVideo);
	state.videoSize = videoAsset->size;

	if (!BuildAccessUnits(state.videoData, state.videoSize, static_cast<size_t>(state.metadata.frameCount), state.accessUnits)) {
		LogError("PS3 VDEC: H.264 stream has invalid AUD/access-unit layout for {}", filename);
		return false;
	}

	if (!state.metadata.audioPath.empty()) {
		auto audioAsset = LoadAsset(state.metadata.audioPath);
		if (!audioAsset.has_value()) {
			LogWarn(LogCategory::Audio, "PS3 VDEC: cannot load extracted movie audio {}: {}",
			    state.metadata.audioPath, audioAsset.error());
			state.metadata.audioPath.clear();
			state.metadata.audioRate = 0;
			state.metadata.audioChannels = 0;
		} else if (audioAsset->size == 0 || (audioAsset->size & 1) != 0) {
			LogWarn(LogCategory::Audio, "PS3 VDEC: invalid raw PCM stream {}", state.metadata.audioPath);
			state.metadata.audioPath.clear();
			state.metadata.audioRate = 0;
			state.metadata.audioChannels = 0;
		} else {
			state.audioData = std::move(audioAsset->data);
			state.audioSize = audioAsset->size;
		}
	}
	return true;
}

void SetCallbackError(VdecMovieState *state, int error)
{
	if (state == nullptr || state->mutex == nullptr)
		return;
	SDL_LockMutex(state->mutex);
	state->callbackError = error == 0 ? -1 : error;
	SDL_UnlockMutex(state->mutex);
}

u32 VdecMovieCallback(u32 handle, u32 messageType, u32 messageData, u32 argument)
{
	auto *state = reinterpret_cast<VdecMovieState *>(static_cast<uintptr_t>(argument));
	if (state == nullptr)
		return 0;

	switch (messageType) {
	case VDEC_CALLBACK_AUDONE:
		SDL_LockMutex(state->mutex);
		state->accessUnitDone = true;
		SDL_UnlockMutex(state->mutex);
		break;
	case VDEC_CALLBACK_PICOUT: {
		u32 pictureAddress = 0;
		const s32 itemResult = vdecGetPicItem(handle, &pictureAddress);
		if (itemResult != 0 || pictureAddress == 0) {
			SetCallbackError(state, itemResult != 0 ? itemResult : VDEC_ERROR_PIC);
			break;
		}
		auto *picture = reinterpret_cast<vdecPicture *>(static_cast<uintptr_t>(pictureAddress));
		if (picture->codec_type != VDEC_CODEC_TYPE_H264 || picture->codec_specific_addr == 0
		    || picture->status != VDEC_PICTURE_NORMAL || picture->attr != 0) {
			SetCallbackError(state, VDEC_ERROR_PIC);
			break;
		}
		auto *h264 = reinterpret_cast<vdecH264Info *>(static_cast<uintptr_t>(picture->codec_specific_addr));
		const int width = h264->width;
		const int height = h264->height;
		if (width <= 0 || height <= 0 || width > ((state->metadata.width + 15) & ~15)
		    || height > ((state->metadata.height + 15) & ~15)) {
			SetCallbackError(state, VDEC_ERROR_PIC);
			break;
		}
		vdecPictureFormat format {};
		format.format_type = VDEC_PICFMT_ARGB32;
		format.color_matrix = VDEC_COLOR_MATRIX_BT709;
		format.alpha = 0xFF;
		const s32 pictureResult = vdecGetPicture(handle, &format, state->decodedFrame);
		if (pictureResult != 0) {
			SetCallbackError(state, pictureResult);
			break;
		}
		SDL_LockMutex(state->mutex);
		state->codedWidth = width;
		state->codedHeight = height;
		state->frameReady = true;
		SDL_UnlockMutex(state->mutex);
		break;
	}
	case VDEC_CALLBACK_SEQDONE:
		SDL_LockMutex(state->mutex);
		state->sequenceDone = true;
		SDL_UnlockMutex(state->mutex);
		break;
	case VDEC_CALLBACK_ERROR:
		SetCallbackError(state, static_cast<int>(messageData));
		break;
	default:
		break;
	}
	return 0;
}

bool OpenVdecDecoder(VdecMovieState &state)
{
	if (state.mutex == nullptr)
		state.mutex = SDL_CreateMutex();
	if (state.mutex == nullptr) {
		LogError("PS3 VDEC: could not create callback mutex: {}", SDL_GetError());
		return false;
	}

	const s32 moduleResult = sysModuleLoad(SYSMODULE_VDEC_H264);
	if (moduleResult != SYSMODULE_OK && moduleResult != SYSMODULE_ERR_DUPLICATE) {
		LogError("PS3 VDEC: sysModuleLoad(H264) failed: 0x{:08x}", static_cast<unsigned int>(moduleResult));
		return false;
	}

	vdecType type {};
	type.codec_type = VDEC_CODEC_TYPE_H264;
	type.profile_level = 42;
	vdecAttr attributes {};
	s32 result = vdecQueryAttr(&type, &attributes);
	if (result != 0 || attributes.mem_size == 0) {
		LogError("PS3 VDEC: vdecQueryAttr failed: 0x{:08x}", static_cast<unsigned int>(result));
		return false;
	}

	const size_t allocationSize = (static_cast<size_t>(attributes.mem_size) + VdecMemoryPageSize - 1)
	    & ~(VdecMemoryPageSize - 1);
	result = sysMemoryAllocate(allocationSize, SYS_MEMORY_PAGE_SIZE_1M, &state.decoderMemory);
	if (result != 0 || state.decoderMemory == 0) {
		LogError("PS3 VDEC: sysMemoryAllocate({}) failed: 0x{:08x}",
		    allocationSize, static_cast<unsigned int>(result));
		return false;
	}

	vdecConfig config {};
	config.mem_addr = static_cast<u32>(state.decoderMemory);
	config.mem_size = attributes.mem_size;
	config.ppu_thread_prio = VdecPpuThreadPriority;
	config.ppu_thread_stack_size = 1 << 14;
	config.spu_thread_prio = VdecSpuThreadPriority;
	config.num_spus = VdecSpuCount;

	vdecClosure closure {};
	closure.fn = static_cast<u32>(__get_opd32(VdecMovieCallback));
	closure.arg = __get_addr32(&state);
	result = vdecOpen(&type, &config, &closure, &state.decoderHandle);
	if (result != 0) {
		LogError("PS3 VDEC: vdecOpen failed: 0x{:08x}", static_cast<unsigned int>(result));
		return false;
	}
	state.decoderOpened = true;
	result = vdecStartSequence(state.decoderHandle);
	if (result != 0) {
		LogError("PS3 VDEC: vdecStartSequence failed: 0x{:08x}", static_cast<unsigned int>(result));
		return false;
	}
	state.sequenceStarted = true;
	LogInfo("PS3 VDEC: H.264 decoder opened, decoder RAM={} bytes, SPUs={}",
	    attributes.mem_size, VdecSpuCount);
	return true;
}

bool AllocateDecodedFrame(VdecMovieState &state)
{
	const size_t alignedWidth = static_cast<size_t>((state.metadata.width + 15) & ~15);
	const size_t alignedHeight = static_cast<size_t>((state.metadata.height + 15) & ~15);
	if (alignedWidth > std::numeric_limits<size_t>::max() / alignedHeight / 4)
		return false;
	state.decodedFrameSize = alignedWidth * alignedHeight * 4;
	void *buffer = AllocateAlignedBuffer(state.decodedFrameSize, VdecOutputAlignment, state.decodedFrameAllocation);
	if (buffer == nullptr) {
		LogError("PS3 VDEC: could not allocate {} bytes for decoded frame", state.decodedFrameSize);
		return false;
	}
	state.decodedFrame = static_cast<Uint8 *>(buffer);
	std::memset(state.decodedFrame, 0, state.decodedFrameSize);
	return true;
}

#ifndef NOSOUND
float MovieVolume()
{
	return static_cast<float>(*GetOptions().Audio.soundVolume - VOLUME_MIN) / -VOLUME_MIN;
}

void StartMovieAudio(VdecMovieState &state)
{
	if (state.audioData == nullptr || state.metadata.audioRate <= 0 || state.metadata.audioChannels <= 0)
		return;
	sound_stop();
	auto decoder = std::make_unique<PushAulibDecoder>(state.metadata.audioChannels, state.metadata.audioRate);
	state.audioDecoder = decoder.get();
	state.audioStream.emplace(nullptr, std::move(decoder),
	    CreateAulibResampler(state.metadata.audioRate), /*closeRw=*/false);
	state.audioStream->setVolume(MovieVolume());
	if (!diablo_is_focused())
		state.audioStream->mute();
	if (!state.audioStream->open()) {
		LogError(LogCategory::Audio, "PS3 VDEC: Aulib stream open failed: {}", SDL_GetError());
		state.audioStream = std::nullopt;
		state.audioDecoder = nullptr;
	} else if (!state.audioStream->play()) {
		LogError(LogCategory::Audio, "PS3 VDEC: Aulib stream play failed: {}", SDL_GetError());
		state.audioStream = std::nullopt;
		state.audioDecoder = nullptr;
	} else {
		LogInfo("PS3 VDEC: PCM audio stream opened {} Hz / {} channel(s)",
		    state.metadata.audioRate, state.metadata.audioChannels);
	}
}

void PushFrameAudio(VdecMovieState &state, size_t frameIndex)
{
	if (state.audioDecoder == nullptr)
		return;
	const uint64_t channelCount = static_cast<uint64_t>(state.metadata.audioChannels);
	const uint64_t numerator = static_cast<uint64_t>(state.metadata.audioRate)
	    * static_cast<uint64_t>(state.metadata.fpsDenominator);
	const uint64_t firstSample = static_cast<uint64_t>(frameIndex) * numerator
	    / static_cast<uint64_t>(state.metadata.fpsNumerator);
	const uint64_t endSample = static_cast<uint64_t>(frameIndex + 1) * numerator
	    / static_cast<uint64_t>(state.metadata.fpsNumerator);
	const uint64_t totalValues = state.audioSize / sizeof(int16_t);
	const uint64_t startValue = firstSample * channelCount;
	const uint64_t endValue = std::min(endSample * channelCount, totalValues);
	if (endValue <= startValue || startValue >= totalValues)
		return;
	const uint64_t valueCount = endValue - startValue;
	if (valueCount > std::numeric_limits<unsigned>::max())
		return;
	const auto *samples = reinterpret_cast<const int16_t *>(state.audioData.get()) + startValue;
	state.audioDecoder->PushSamples(samples, static_cast<unsigned>(valueCount));
}
#else
void StartMovieAudio(VdecMovieState &) {}
void PushFrameAudio(VdecMovieState &, size_t) {}
#endif

void RestoreGameTexture(VdecMovieState &state)
{
	if (!state.textureReplaced || renderer == nullptr)
		return;
	LogInfo("PS3 VDEC: restoring game render texture");
	texture = SDLWrap::CreateTexture(renderer, DEVILUTIONX_DISPLAY_TEXTURE_FORMAT,
	    SDL_TEXTUREACCESS_STREAMING, gnScreenWidth, gnScreenHeight);
	if (texture == nullptr) {
		LogError("PS3 VDEC: game texture restore failed: {}", SDL_GetError());
	} else if (SDL_RenderSetLogicalSize(renderer, gnScreenWidth, gnScreenHeight) <= -1) {
		LogError("PS3 VDEC: restoring game logical size failed: {}", SDL_GetError());
	}
	state.textureReplaced = false;
}

bool EndVdecSequenceAndWait(VdecMovieState &state)
{
	SDL_LockMutex(state.mutex);
	state.sequenceDone = false;
	SDL_UnlockMutex(state.mutex);
	const s32 endResult = vdecEndSequence(state.decoderHandle);
	if (endResult != 0) {
		LogWarn("PS3 VDEC: vdecEndSequence returned 0x{:08x}", static_cast<unsigned int>(endResult));
		return false;
	}

	// The semaphore-backed SDL2 condition variable can fail in sysSemWait on
	// PSL1GHT before a VDEC callback arrives. Poll callback state under the
	// existing mutex instead, keeping the asynchronous wait bounded.
	const Uint32 start = SDL_GetTicks();
	for (;;) {
		SDL_LockMutex(state.mutex);
		const bool complete = state.sequenceDone;
		SDL_UnlockMutex(state.mutex);
		if (complete)
			return true;
		if (SDL_GetTicks() - start >= VdecTimeoutMs)
			break;
		SDL_Delay(1);
	}
	LogWarn("PS3 VDEC: sequence-end callback timed out");
	return false;
}

bool RestartVdecSequence(VdecMovieState &state)
{
	if (!EndVdecSequenceAndWait(state))
		return false;
	state.sequenceStarted = false;
	const s32 result = vdecStartSequence(state.decoderHandle);
	if (result != 0) {
		LogError("PS3 VDEC: vdecStartSequence during loop failed: 0x{:08x}", static_cast<unsigned int>(result));
		return false;
	}
	state.sequenceStarted = true;
	state.frameIndex = 0;
	state.startTicks = SDL_GetTicks();
	state.codedWidth = 0;
	state.codedHeight = 0;
#ifndef NOSOUND
	if (state.audioDecoder != nullptr)
		state.audioDecoder->DiscardPendingSamples();
#endif
	LogInfo("PS3 VDEC: restarted looping movie sequence");
	return true;
}

void CloseVdecDecoder(VdecMovieState &state)
{
	if (state.decoderOpened) {
		if (state.sequenceStarted) {
			EndVdecSequenceAndWait(state);
			state.sequenceStarted = false;
		}
		const s32 closeResult = vdecClose(state.decoderHandle);
		if (closeResult != 0)
			LogWarn("PS3 VDEC: vdecClose returned 0x{:08x}", static_cast<unsigned int>(closeResult));
		state.decoderOpened = false;
	}
	if (state.decoderMemory != 0) {
		const s32 freeResult = sysMemoryFree(state.decoderMemory);
		if (freeResult != 0)
			LogWarn("PS3 VDEC: sysMemoryFree returned 0x{:08x}", static_cast<unsigned int>(freeResult));
		state.decoderMemory = 0;
	}
}

void DestroyVdecMovieState(VdecMovieState &state, bool restoreTexture)
{
#ifndef NOSOUND
	if (state.audioDecoder != nullptr) {
		LogInfo("PS3 VDEC audio queue: high_water={} samples limit={} samples dropped={} samples",
		    state.audioDecoder->QueueHighWaterSamples(),
		    state.audioDecoder->QueueLimitSamples(),
		    state.audioDecoder->QueueDroppedSamples());
	}
	state.audioStream = std::nullopt;
	state.audioDecoder = nullptr;
#endif
	CloseVdecDecoder(state);
	if (restoreTexture)
		RestoreGameTexture(state);
	if (state.mutex != nullptr) {
		SDL_DestroyMutex(state.mutex);
		state.mutex = nullptr;
	}
	std::free(state.decodedFrameAllocation);
	state.decodedFrameAllocation = nullptr;
	state.decodedFrame = nullptr;
	std::free(state.videoDataAllocation);
	state.videoDataAllocation = nullptr;
	state.videoData = nullptr;
}

bool ReplaceMovieTexture(VdecMovieState &state)
{
	if (renderer == nullptr) {
		LogError("PS3 VDEC: SDL renderer is unavailable");
		return false;
	}
	SDLTextureUniquePtr replacementTexture {
		SDL_CreateTexture(renderer, DEVILUTIONX_DISPLAY_TEXTURE_FORMAT,
		    SDL_TEXTUREACCESS_STREAMING, state.metadata.width, state.metadata.height)
	};
	if (replacementTexture == nullptr) {
		LogError("PS3 VDEC: SDL texture creation failed: {}", SDL_GetError());
		return false;
	}
	texture.swap(replacementTexture);
	replacementTexture.reset();
	state.textureReplaced = true;
	if (SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_NONE) < 0) {
		LogError("PS3 VDEC: disabling movie texture blending failed: {}", SDL_GetError());
		return false;
	}
	if (SDL_RenderSetLogicalSize(renderer, state.metadata.width, state.metadata.height) <= -1) {
		LogError("PS3 VDEC: SDL_RenderSetLogicalSize failed: {}", SDL_GetError());
		return false;
	}
	if (SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE) < 0 || SDL_RenderClear(renderer) < 0) {
		LogError("PS3 VDEC: clearing movie frame failed: {}", SDL_GetError());
		return false;
	}
	SDL_RenderPresent(renderer);
	return true;
}

bool WaitForDecodedAccessUnit(VdecMovieState &state)
{
	const Uint32 start = SDL_GetTicks();
	for (;;) {
		SDL_LockMutex(state.mutex);
		const bool complete = state.accessUnitDone && state.frameReady;
		const int error = state.callbackError;
		const bool accessUnitDone = state.accessUnitDone;
		const bool frameReady = state.frameReady;
		SDL_UnlockMutex(state.mutex);

		if (error != 0) {
			LogError("PS3 VDEC: decoder callback error on frame {} (error=0x{:08x}, au_done={}, frame_ready={})",
			    state.frameIndex, static_cast<unsigned int>(error), accessUnitDone, frameReady);
			return false;
		}
		if (complete)
			return true;
		if (SDL_GetTicks() - start >= VdecTimeoutMs) {
			LogError("PS3 VDEC: timed out waiting for frame {} callbacks (au_done={}, frame_ready={})",
			    state.frameIndex, accessUnitDone, frameReady);
			return false;
		}
		SDL_Delay(1);
	}
}

} // namespace

bool PS3VdecMovieBegin(const char *filename, int flags)
{
	if (filename == nullptr || (flags & 0x10000) != 0 || (flags & 0x20000000) != 0)
		return false;
	if (ActiveVdecMovie != nullptr)
		PS3VdecMovieEnd();

	auto state = std::make_unique<VdecMovieState>();
	state->loop = (flags & 0x40000) != 0;
	if (!LoadMovieAssets(filename, *state)) {
		DestroyVdecMovieState(*state, false);
		return false;
	}
	if (!AllocateDecodedFrame(*state)) {
		DestroyVdecMovieState(*state, false);
		return false;
	}
	if (!OpenVdecDecoder(*state)) {
		DestroyVdecMovieState(*state, false);
		return false;
	}
	if (!ReplaceMovieTexture(*state)) {
		DestroyVdecMovieState(*state, true);
		return false;
	}
	if ((flags & 0x1000000) == 0)
		StartMovieAudio(*state);
	state->startTicks = SDL_GetTicks();
	LogInfo("PS3 VDEC: starting {} ({}x{}, {} fps, {} frames)",
	    filename, state->metadata.width, state->metadata.height,
	    static_cast<double>(state->metadata.fpsNumerator) / state->metadata.fpsDenominator,
	    state->metadata.frameCount);
	ActiveVdecMovie = std::move(state);
	return true;
}

bool PS3VdecMovieContinue()
{
	if (ActiveVdecMovie == nullptr)
		return false;
	VdecMovieState &state = *ActiveVdecMovie;
	if (state.frameIndex >= state.accessUnits.size())
		return false;

	const AccessUnit &accessUnit = state.accessUnits[state.frameIndex];
	SDL_LockMutex(state.mutex);
	state.accessUnitDone = false;
	state.frameReady = false;
	state.callbackError = 0;
	SDL_UnlockMutex(state.mutex);

	const uint64_t pts = static_cast<uint64_t>(state.frameIndex) * 1000000ULL
	    * static_cast<uint64_t>(state.metadata.fpsDenominator)
	    / static_cast<uint64_t>(state.metadata.fpsNumerator);
	vdecAU au {};
	au.packet_addr = __get_addr32(state.videoData + accessUnit.offset);
	au.packet_size = static_cast<u32>(accessUnit.size);
	au.pts.low = static_cast<u32>(pts);
	au.pts.hi = static_cast<u32>(pts >> 32);
	au.dts = au.pts;
	au.userdata = state.frameIndex;

	const s32 decodeResult = vdecDecodeAu(state.decoderHandle, VDEC_DECODER_MODE_NORMAL, &au);
	if (decodeResult != 0) {
		LogError("PS3 VDEC: vdecDecodeAu failed on frame {}: 0x{:08x}",
		    state.frameIndex, static_cast<unsigned int>(decodeResult));
		return false;
	}
	if (!WaitForDecodedAccessUnit(state))
		return false;
	if (state.codedWidth < state.metadata.width || state.codedHeight < state.metadata.height) {
		LogError("PS3 VDEC: decoded dimensions {}x{} are smaller than visible frame {}x{}",
		    state.codedWidth, state.codedHeight, state.metadata.width, state.metadata.height);
		return false;
	}

	PushFrameAudio(state, state.frameIndex);
	if (!RenderDecodedVideoFrame(state.decodedFrame, state.metadata.width, state.metadata.height, state.codedWidth * 4)) {
		LogError("PS3 VDEC: failed to present decoded frame {}", state.frameIndex);
		return false;
	}
	++state.frameIndex;
	const bool reachedEnd = state.frameIndex >= state.accessUnits.size();

	// Keep the final image and its trailing audio alive for one complete frame
	// interval, matching the original Smacker playback timing.
	const uint64_t frameElapsedMs = static_cast<uint64_t>(state.frameIndex) * 1000ULL
	    * static_cast<uint64_t>(state.metadata.fpsDenominator)
	    / static_cast<uint64_t>(state.metadata.fpsNumerator);
	const Uint32 elapsed = SDL_GetTicks() - state.startTicks;
	if (frameElapsedMs > elapsed) {
		const Uint64 delay = frameElapsedMs - elapsed;
		SDL_Delay(static_cast<Uint32>(std::min<Uint64>(delay, std::numeric_limits<Uint32>::max())));
	}
	if (reachedEnd) {
		if (!state.loop)
			return false;
		if (!RestartVdecSequence(state))
			return false;
	}
	return true;
}

void PS3VdecMovieEnd()
{
	if (ActiveVdecMovie == nullptr)
		return;
	LogInfo("PS3 VDEC: stopping movie after {} frame(s)", ActiveVdecMovie->frameIndex);
	DestroyVdecMovieState(*ActiveVdecMovie, true);
	ActiveVdecMovie.reset();
}

} // namespace devilution

#endif
