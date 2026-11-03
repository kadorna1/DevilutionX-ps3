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
#include <sys/file.h>
#include <sys/tty.h>
#include <lv2/sysfs.h>

#ifndef DEVILUTIONX_PS3_TITLE_ID
#define DEVILUTIONX_PS3_TITLE_ID "DEVILUT00"
#endif

namespace {

s32 PS3LogFd = -1;

void PS3RawWriteToFd(s32 fd, const char *text, u64 length)
{
	if (fd < 0 || text == nullptr)
		return;
	while (length > 0) {
		u64 written = 0;
		const s32 result = sysLv2FsWrite(fd, text, length, &written);
		if (result != 0 || written == 0)
			return;
		text += written;
		length -= written;
	}
}

void PS3RawWriteToTty(const char *text, u32 length)
{
	if (text == nullptr || length == 0)
		return;
	u32 written = 0;
	(void)sysTtyWrite(0, text, length, &written);
}

void PS3RawWrite(const char *text, u64 length)
{
	PS3RawWriteToFd(PS3LogFd, text, length);
}

void PS3RawWrite(const char *text)
{
	if (text == nullptr)
		return;
	u64 length = 0;
	while (text[length] != '\0')
		++length;
	PS3RawWrite(text, length);
}

void PS3RawWriteNumber(s32 value)
{
	char digits[16];
	unsigned int number;
	unsigned int count = 0;
	if (value < 0) {
		PS3RawWrite("-");
		number = static_cast<unsigned int>(-(value + 1)) + 1;
	} else {
		number = static_cast<unsigned int>(value);
	}
	do {
		digits[count++] = static_cast<char>('0' + number % 10);
		number /= 10;
	} while (number != 0 && count < sizeof(digits));
	while (count > 0) {
		const char digit = digits[--count];
		PS3RawWrite(&digit, 1);
	}
}

void PS3WriteBootLog(const char *message)
{
	PS3RawWrite("[PS3][BOOT] ");
	PS3RawWrite(message);
	PS3RawWrite("\n");
}

void PS3LogOutput(void *, int category, SDL_LogPriority priority, const char *message)
{
	PS3RawWrite("[SDL][category=");
	PS3RawWriteNumber(category);
	PS3RawWrite("][priority=");
	PS3RawWriteNumber(static_cast<s32>(priority));
	PS3RawWrite("] ");
	PS3RawWrite(message != nullptr ? message : "");
	PS3RawWrite("\n");
}

void InitializePS3Log()
{
	if (PS3LogFd >= 0)
		return;

	// Bypass Newlib entirely: these wrappers issue LV2 syscalls directly.
	static const char *const candidates[] = {
	    "/dev_hdd0/game/" DEVILUTIONX_PS3_TITLE_ID "/USRDIR/devilutionx-ps3.log",
	    "/dev_hdd0/tmp/devilutionx-ps3.log",
	    "/dev_hdd0/game/" DEVILUTIONX_PS3_TITLE_ID "/devilutionx-ps3.log",
	    "/dev_usb000/devilutionx-ps3.log",
	    "devilutionx-ps3.log",
	};
	s32 openResults[sizeof(candidates) / sizeof(candidates[0])] = {};

	for (unsigned int index = 0; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
		s32 fd = -1;
		const s32 result = sysLv2FsOpen(candidates[index], SYS_O_WRONLY | SYS_O_CREAT | SYS_O_TRUNC, &fd, 0666, nullptr, 0);
		if (result != 0) {
			openResults[index] = result;
			continue;
		}

		PS3LogFd = fd;
		PS3WriteBootLog("direct LV2 filesystem logger opened");
		for (unsigned int failed = 0; failed < index; ++failed) {
			PS3RawWrite("[PS3][BOOT] sysLv2FsOpen failed: ");
			PS3RawWrite(candidates[failed]);
			PS3RawWrite(" (LV2 result=");
			PS3RawWriteNumber(openResults[failed]);
			PS3RawWrite(")\n");
		}
		PS3RawWrite("[PS3][BOOT] log path: ");
		PS3RawWrite(candidates[index]);
		PS3RawWrite("\n");
		PS3WriteBootLog("pre-main constructor reached");
		return;
	}

	// If no HDD/USB path can be opened, leave a TTY diagnostic for RPCS3,
	// ethdebug, or a development host listening to PSL1GHT's TTY stream.
	for (unsigned int index = 0; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
		PS3RawWriteToTty("[PS3][BOOT] sysLv2FsOpen failed (result=", 42);
		char digits[16];
		unsigned int number = openResults[index] < 0 ? static_cast<unsigned int>(-(openResults[index] + 1)) + 1 : static_cast<unsigned int>(openResults[index]);
		unsigned int count = 0;
		do {
			digits[count++] = static_cast<char>('0' + number % 10);
			number /= 10;
		} while (number != 0 && count < sizeof(digits));
		if (openResults[index] < 0)
			PS3RawWriteToTty("-", 1);
		while (count > 0) {
			const char digit = digits[--count];
			PS3RawWriteToTty(&digit, 1);
		}
		PS3RawWriteToTty(") path=", 7);
		u64 pathLength = 0;
		while (candidates[index][pathLength] != '\0')
			++pathLength;
		PS3RawWriteToTty(candidates[index], static_cast<u32>(pathLength));
		PS3RawWriteToTty("\n", 1);
	}
}

// Priority 103 runs before PSL1GHT's priority-104 Newlib registration.
// The first diagnostic uses direct LV2 syscalls and does not depend on
// Newlib's __syscalls table or FILE/stdio initialization.
__attribute__((constructor(103))) void PS3EarlyLogConstructor()
{
	InitializePS3Log();
}

} // namespace
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
// PSL1GHT's __syscalls_init runs at constructor priority 104.
// Run after it so Newlib fopen() has working LV2 filesystem callbacks.
__attribute__((constructor(105))) void PS3EarlyLogConstructor()
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
