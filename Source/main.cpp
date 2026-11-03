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

#ifndef DEVILUTIONX_PS3_TITLE_ID
#define DEVILUTIONX_PS3_TITLE_ID "DEVILUT00"
#endif

namespace {

FILE *PS3LogFile = nullptr;

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
	// Open the persistent log before SDL/game initialization, so early failures and
	// runtime diagnostics survive a launch that returns directly to XMB.
	std::fflush(stdout);
	std::fflush(stderr);

	const char *logPath = "/dev_hdd0/game/" DEVILUTIONX_PS3_TITLE_ID "/USRDIR/devilutionx-ps3.log";
	PS3LogFile = std::fopen(logPath, "w");
	if (PS3LogFile == nullptr) {
		logPath = "devilutionx-ps3.log";
		PS3LogFile = std::fopen(logPath, "w");
	}
	if (PS3LogFile == nullptr) {
		std::fprintf(stderr, "[PS3][BOOT] Could not open startup log in USRDIR or current directory.\n");
		std::fflush(stderr);
		return;
	}

	// freopen is provided by Newlib; dup2 is not available in the PS3 SDK.
	std::setvbuf(PS3LogFile, nullptr, _IONBF, 0);
	if (std::freopen(logPath, "a", stdout) == nullptr)
		PS3WriteBootLog("warning: could not redirect stdout");
	else
		std::setvbuf(stdout, nullptr, _IONBF, 0);
	if (std::freopen(logPath, "a", stderr) == nullptr)
		PS3WriteBootLog("warning: could not redirect stderr");
	else
		std::setvbuf(stderr, nullptr, _IONBF, 0);

	SDL_LogSetOutputFunction(PS3LogOutput, nullptr);
	SDL_LogSetAllPriority(SDL_LOG_PRIORITY_VERBOSE);
	PS3WriteBootLog(logPath);
}} // namespace
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
	char bootMessage[96];
	std::snprintf(bootMessage, sizeof(bootMessage), "main() entered; argc=%d", argc);
	PS3WriteBootLog(bootMessage);
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
