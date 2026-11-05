#!/usr/bin/env python3
"""Raise the SDL2 PSL1GHT RSX context to the hardware-tested PS3 allocation."""

from pathlib import Path
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: patch_sdl2_psl1ght_video.py SDL_PSL1GHTvideo.c")

path = Path(sys.argv[1])
source = path.read_text(encoding="utf-8")


def replace_once(old: str, new: str, description: str) -> None:
    global source
    if new in source:
        return
    count = source.count(old)
    if count != 1:
        raise SystemExit(
            f"error: expected exactly one upstream match for {description}; found {count}"
        )
    source = source.replace(old, new, 1)


replace_once(
    "#define PSL1GHTVID_DRIVER_NAME \"psl1ght\"",
    """/* Match the RSX allocation validated by the Xash3D PS3 backend.
 * The upstream 64 KiB command buffer / 1 MiB I/O area stalled on real hardware.
 */
#define PSL1GHT_RSX_CB_SIZE   (1U * 1024U * 1024U)
#define PSL1GHT_RSX_HOST_SIZE (8U * 1024U * 1024U)

#define PSL1GHTVID_DRIVER_NAME \"psl1ght\"""",
    "RSX allocation constants",
)

replace_once(
    "static void initializeGPU( SDL_DeviceData * devdata);",
    "static int initializeGPU( SDL_DeviceData * devdata);",
    "initializeGPU declaration",
)

replace_once(
    """    initializeGPU(devdata);
    PSL1GHT_InitModes(_this);""",
    """    if (initializeGPU(devdata) < 0) {
        PSL1GHT_QuitSysEvent(_this);
        SDL_free(devdata);
        _this->driverdata = NULL;
        return -1;
    }
    PSL1GHT_InitModes(_this);""",
    "propagate RSX initialization failures",
)

replace_once(
    "void initializeGPU( SDL_DeviceData * devdata)\n{\n    deprintf (1, \"initializeGPU()\\n\");\n   // Allocate a 1Mb buffer, alligned to a 1Mb boundary to be our shared IO memory with the RSX.\n    void *host_addr = memalign(1024*1024, 1024*1024);\n    assert(host_addr != NULL);\n\n    // Initilise Reality, which sets up the command buffer and shared IO memory\n    rsxInit(&devdata->_CommandBuffer, 0x10000, 1024*1024, host_addr);\n    assert(devdata->_CommandBuffer != NULL);\n}",
    """static int initializeGPU( SDL_DeviceData * devdata)
{
    void *host_addr;
    s32 ret;

    deprintf (1, "initializeGPU()\\n");

    /* The shared RSX I/O area must remain allocated for the lifetime of GCM. */
    host_addr = memalign(1024U * 1024U, PSL1GHT_RSX_HOST_SIZE);
    if (host_addr == NULL)
        return SDL_OutOfMemory();

    ret = rsxInit(&devdata->_CommandBuffer, PSL1GHT_RSX_CB_SIZE,
                  PSL1GHT_RSX_HOST_SIZE, host_addr);
    SDL_Log("PSL1GHT RSX init: ret=%d context=%p command_buffer=%u host_buffer=%u",
        (int)ret, (void *)devdata->_CommandBuffer,
        (unsigned int)PSL1GHT_RSX_CB_SIZE, (unsigned int)PSL1GHT_RSX_HOST_SIZE);

    if (ret != 0 || devdata->_CommandBuffer == NULL) {
        free(host_addr);
        return SDL_SetError("Could not initialize the PSL1GHT RSX command context");
    }

    /* host_addr intentionally remains live until process exit; RSX uses it. */
    return 0;
}""",
    "RSX context allocation",
)

for required in (
    "#define PSL1GHT_RSX_CB_SIZE   (1U * 1024U * 1024U)",
    "#define PSL1GHT_RSX_HOST_SIZE (8U * 1024U * 1024U)",
    "rsxInit(&devdata->_CommandBuffer, PSL1GHT_RSX_CB_SIZE,",
    "PSL1GHT_RSX_HOST_SIZE, host_addr)",
    "PSL1GHT RSX init: ret=%d",
    "PSL1GHT_QuitSysEvent(_this);",
):
    if required not in source:
        raise SystemExit("error: required RSX initialization change missing: " + required)

if "rsxInit(&devdata->_CommandBuffer, 0x10000, 1024*1024, host_addr)" in source:
    raise SystemExit("error: obsolete 64 KiB / 1 MiB RSX initialization remains")

path.write_text(source, encoding="utf-8")
print("Applied 1 MiB RSX command buffer and 8 MiB shared I/O area")
