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
#include "storm/ps3_vdec.h"
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
	vdec_handle_unused_not_used_placeholder;
};

} // namespace
} // namespace devilution

#endif
