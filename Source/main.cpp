#ifdef USE_SDL3
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#else
#include <SDL.h>
#include <SDL_main.h>
#endif

#ifdef __SWITCH__
#include "platform/switch/network.h"
#include "platform/switch/random.hpp"
#include "platform/switch/romfs.hpp"
#endif
#ifdef __3DS__
#include "platform/ctr/system.h"
#endif
#ifdef __vita__
#include "platform/vita/network.h"
#include "platform/vita/random.hpp"
#endif
#ifdef NXDK
#include <nxdk/mount.h>
#endif
#ifdef GPERF_HEAP_MAIN
#include <gperftools/heap-profiler.h>
#endif

#include "diablo.h"

#ifdef __PS3__
#include <cerrno>
#include <cstdio>

#ifndef DEVILUTIONX_PS3_TITLE_ID
#define DEVILUTIONX_PS3_TITLE_ID "DEVILUT00"
#endif

namespace {

FILE *PS3LogFile = nullptr;
const char *PS3LogPath = nullptr;

void PS3WriteBootLog(const char *message)
{
	FILE *stream = PS3LogFile != nullptr ? PS3LogFile : stderr;
	std::fprintf(stream, "[PS3][BOOT] %s\n", message != nullptr ? message : "");
	std::fflush(stream);
}

void PS3LogOutput(void *, int category, SDL_LogPriority priority, const char *message)
{
	FILE *stream = PS3LogFile != nullptr ? PS3LogFile : stderr;
	std::fprintf(stream, "[SDL][category=%d][priority=%d] %s\n", category, static_cast<int>(priority), message != nullptr ? message : "");
	std::fflush(stream);
}

void InitializePS3Log()
{
	if (PS3LogFile != nullptr)
		return;

	// Try the installed title directory first, then independent HDD/USB locations.
	// Some CFW/HEN setups allow reading USRDIR but deny creating files there.
	static const char *const candidates[] = {
	    "/dev_hdd0/game/" DEVILUTIONX_PS3_TITLE_ID "/USRDIR/devilutionx-ps3.log",
	    "/dev_hdd0/tmp/devilutionx-ps3.log",
	    "/dev_hdd0/game/" DEVILUTIONX_PS3_TITLE_ID "/devilutionx-ps3.log",
	    "/dev_usb000/devilutionx-ps3.log",
	    "devilutionx-ps3.log",
	};
	int openErrors[sizeof(candidates) / sizeof(candidates[0])] = {};

	std::fflush(stdout);
	std::fflush(stderr);

	for (unsigned int index = 0; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
		const char *candidate = candidates[index];
		errno = 0;
		FILE *file = std::fopen(candidate, "w");
		if (file == nullptr) {
			openErrors[index] = errno;
			continue;
		}

		PS3LogFile = file;
		PS3LogPath = candidate;
		std::setvbuf(PS3LogFile, nullptr, _IONBF, 0);
		PS3WriteBootLog("persistent log opened");

		// Record earlier failures in the persistent log, rather than only on stderr.
		for (unsigned int failedIndex = 0; failedIndex < index; ++failedIndex) {
			if (openErrors[failedIndex] != 0) {
				std::fprintf(PS3LogFile, "[PS3][BOOT] fopen failed: %s (errno=%d)\n", candidates[failedIndex], openErrors[failedIndex]);
			}
		}
		PS3WriteBootLog(PS3LogPath);
		std::fflush(PS3LogFile);
		return;
	}

	for (unsigned int index = 0; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
		std::fprintf(stderr, "[PS3][BOOT] fopen failed: %s (errno=%d)\n", candidates[index], openErrors[index]);
	}
	std::fprintf(stderr, "[PS3][BOOT] FATAL: unable to create a log in any candidate location.\n");
	std::fflush(stderr);
}

// Run before main(), so the log still exists if application initialization fails
// before the normal entry point can install the SDL log callback.
__attribute__((constructor(101))) void PS3EarlyLogConstructor()
{
	InitializePS3Log();
	PS3WriteBootLog("pre-main constructor reached");
}

} // namespace
#endif

#if !defined(__APPLE__)
extern "C" const char *__asan_default_options() // NOLINT(bugprone-reserved-identifier, readability-identifier-naming)
{
	return "halt_on_error=0";
}
#endif

extern "C" int main(int argc, char **argv)
{
#ifdef __PS3__
	InitializePS3Log();
	SDL_LogSetOutputFunction(PS3LogOutput, nullptr);
	SDL_LogSetAllPriority(SDL_LOG_PRIORITY_VERBOSE);
	PS3WriteBootLog("main() entered");
#endif
#ifdef __SWITCH__
	switch_romfs_init();
	switch_enable_network();
#ifdef PACKET_ENCRYPTION
	randombytes_switchrandom_init();
#endif
#endif
#ifdef __3DS__
	ctr_sys_init();
#endif
#ifdef __vita__
	vita_enable_network();
#ifdef PACKET_ENCRYPTION
	randombytes_vitarandom_init();
#endif
#endif
#ifdef NXDK
	nxMountDrive('E', "\\Device\\Harddisk0\\Partition1\\");
#endif
#ifdef GPERF_HEAP_MAIN
	HeapProfilerStart("main");
#endif
#ifdef __PS3__
	SDL_Log("PS3 boot: entering DiabloMain");
#endif
	const int result = devilution::DiabloMain(argc, argv);
#ifdef __PS3__
	SDL_Log("PS3 boot: DiabloMain returned with status %d", result);
#endif
#ifdef GPERF_HEAP_MAIN
	HeapProfilerStop();
#endif
	return result;
}
