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
#include <ppu-lv2.h>
#include <lv2/sysfs.h>
#include <sys/process.h>

// Declare the process priority and main-thread stack expected by the PS3 loader.
// The linker configuration below preserves this record in Release builds.
SYS_PROCESS_PARAM(1001, 0x100000);

#ifndef DEVILUTIONX_PS3_TITLE_ID
#define DEVILUTIONX_PS3_TITLE_ID "DEVILUT00"
#endif

namespace {

s32 PS3LogFd = -1;

s32 PS3SysLv2FsOpen(const char *path, s32 flags, s32 *fd, u32 mode)
{
	lv2syscall6(801, (u64)path, flags, (u64)fd, mode, 0, 0);
	return_to_user_prog(s32);
}

s32 PS3SysLv2FsWrite(s32 fd, const void *buffer, u64 length, u64 *written)
{
	lv2syscall4(803, fd, (u64)buffer, length, (u64)written);
	return_to_user_prog(s32);
}

s32 PS3SysLv2FsFsync(s32 fd)
{
	lv2syscall1(820, fd);
	return_to_user_prog(s32);
}

void PS3RawWriteToFd(s32 fd, const char *text, u64 length)
{
	if (fd < 0 || text == nullptr)
		return;
	while (length > 0) {
		u64 written = 0;
		const s32 result = PS3SysLv2FsWrite(fd, text, length, &written);
		if (result != 0 || written == 0)
			return;
		text += written;
		length -= written;
	}
}

void PS3RawWrite(const char *text, u64 length)
{
	// Diagnostics are written only to the console HDD or mounted USB storage.
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
	if (PS3LogFd >= 0)
		(void)PS3SysLv2FsFsync(PS3LogFd);

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
	    "/dev_usb001/devilutionx-ps3.log",
	    "/dev_usb002/devilutionx-ps3.log",
	    "/dev_usb003/devilutionx-ps3.log",
	    "/dev_usb004/devilutionx-ps3.log",
	    "/dev_usb005/devilutionx-ps3.log",
	};
	s32 openResults[sizeof(candidates) / sizeof(candidates[0])] = {};

	for (unsigned int index = 0; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
		s32 fd = -1;
		const s32 result = PS3SysLv2FsOpen(candidates[index], SYS_O_WRONLY | SYS_O_CREAT | SYS_O_TRUNC, &fd, 0666);
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
		(void)PS3SysLv2FsFsync(PS3LogFd);
		return;
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

extern "C" void PS3LuaRawBootLog(const char *message)
{
	PS3WriteBootLog(message);
	if (PS3LogFd >= 0)
		(void)PS3SysLv2FsFsync(PS3LogFd);
}
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
