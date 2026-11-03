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
#include <cstdio>
#include <unistd.h>

#ifndef DEVILUTIONX_PS3_TITLE_ID
#define DEVILUTIONX_PS3_TITLE_ID "DEVILUT00"
#endif

namespace {

void PS3LogOutput(void *, int category, SDL_LogPriority priority, const char *message)
{
	std::fprintf(stderr, "[SDL][category=%d][priority=%d] %s\n", category, static_cast<int>(priority), message != nullptr ? message : "");
	std::fflush(stderr);
}

void InitializePS3Log()
{
	// Capture process output before DiabloMain initializes the game. This also keeps
	// C/C++ runtime diagnostics that do not use SDL_Log in the same persistent file.
	std::fflush(stdout);
	std::fflush(stderr);

	const char *logPath = "/dev_hdd0/game/" DEVILUTIONX_PS3_TITLE_ID "/USRDIR/devilutionx-ps3.log";
	FILE *logFile = std::fopen(logPath, "w");
	if (logFile == nullptr) {
		logPath = "devilutionx-ps3.log";
		logFile = std::fopen(logPath, "w");
	}
	if (logFile == nullptr) {
		std::fprintf(stderr, "[PS3][BOOT] Could not open startup log in USRDIR or current directory.\n");
		std::fflush(stderr);
		return;
	}

	const int logFd = fileno(logFile);
	const int stdoutResult = dup2(logFd, STDOUT_FILENO);
	const int stderrResult = dup2(logFd, STDERR_FILENO);
	std::fclose(logFile);

	std::setvbuf(stdout, nullptr, _IONBF, 0);
	std::setvbuf(stderr, nullptr, _IONBF, 0);

	if (stdoutResult < 0 || stderrResult < 0) {
		std::fprintf(stderr, "[PS3][BOOT] Warning: failed to redirect stdout/stderr to the startup log.\n");
		std::fflush(stderr);
	}

	SDL_LogSetOutputFunction(PS3LogOutput, nullptr);
	SDL_LogSetAllPriority(SDL_LOG_PRIORITY_VERBOSE);
	std::fprintf(stderr, "[PS3][BOOT] Startup log opened: %s\n", logPath);
	std::fflush(stderr);
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
	std::fprintf(stderr, "[PS3][BOOT] main() entered; argc=%d\n", argc);
	std::fflush(stderr);
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
