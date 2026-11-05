#!/usr/bin/env python3
"""Apply the verified PS3 synchronization and safety adaptations to SDL_PSL1GHT."""

from pathlib import Path
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: patch_sdl2_psl1ght_renderer.py SDL_PSL1GHTrender.c")

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


# The backend has exactly two display buffers. Completion callbacks are the
# reliable signal for when the previous display buffer may be reused.
replace_once(
    """typedef struct
{
    bool first_fb; // Is this the first flip ?
    int current_screen;
    SDL_Surface *screens[3];
    void *textures[3];
    gcmContextData *context; // Context to keep track of the RSX buffer.
} PSL1GHT_RenderData;""",
    """typedef struct
{
    bool first_fb; // Is this the first flip ?
    int current_screen;
    SDL_Surface *screens[3];
    void *textures[3];
    gcmContextData *context; // Context to keep track of the RSX buffer.
    volatile u32 flip_queued;
    volatile u32 flip_completed;
    volatile u32 *idle_label;
    u32 idle_fence_value;
    size_t display_bytes;
    size_t texture_bytes;
    size_t texture_peak_bytes;
    unsigned int texture_count;
    unsigned int texture_peak_count;
} PSL1GHT_RenderData;""",
    "renderer state structure",
)

replace_once(
    """typedef struct
{
    SDL_Rect    srcRect;
    SDL_Rect   dstRect;
} PSL1GHT_CopyData;


static void waitFlip()
{
    while (gcmGetFlipStatus() != 0)
      usleep(200);
    gcmResetFlipStatus();
}""",
    """typedef struct
{
    SDL_Rect    srcRect;
    SDL_Rect   dstRect;
} PSL1GHT_CopyData;

/*
 * Label 68 is in the user-available GCM label range. This backend uses it
 * for a monotonically increasing RSX backend fence, so texture memory is
 * released only after all earlier RSX reads have completed.
 */
#define PSL1GHT_WAIT_IDLE_LABEL 255

/*
 * rsxkit-style flip retirement: wait on the PPE for the previous flip,
 * then let RenderPresent reset status and queue the next flip. Do not insert
 * gcmSetWaitFlip into the RSX command stream: rsxkit deliberately keeps the
 * wait outside the FIFO so the system's own display work can continue.
 */
static SDL_bool
PSL1GHT_WaitForFreeBuffer(PSL1GHT_RenderData *data)
{
    int waited = 0;
    u32 status = 0;

    if (data == NULL)
        return SDL_TRUE;

    while ((status = gcmGetFlipStatus()) != 0) {
        if (++waited >= 20000) {
            gcmControlRegister *control = gcmGetControlRegister();
            u32 get = control != NULL ? control->get : 0;
            u32 put = control != NULL ? control->put : 0;
            SDL_Log("PSL1GHT flip timeout: status=%u queued=%u completed=%u buffer=%d get=0x%08x put=0x%08x",
                (unsigned int)status, (unsigned int)data->flip_queued,
                (unsigned int)data->flip_completed, data->current_screen,
                (unsigned int)get, (unsigned int)put);
            SDL_SetError("PSL1GHT timed out waiting for display flip completion");
            return SDL_FALSE;
        }
        usleep(200);
    }

    data->flip_completed = data->flip_queued;
    return SDL_TRUE;
}

/*
 * A flip callback protects scanout buffers, but texture source memory may
 * still be read by an earlier transfer command. A backend label is written
 * after all prior RSX work and therefore provides the required DMA fence.
 */
static SDL_bool
PSL1GHT_WaitForRSXIdle(PSL1GHT_RenderData *data, const char *operation)
{
    u32 value;
    int waited = 0;

    if (data == NULL || data->context == NULL || data->idle_label == NULL) {
        SDL_SetError("PSL1GHT RSX idle fence is unavailable");
        return SDL_FALSE;
    }

    value = ++data->idle_fence_value;
    if (value == 0)
        value = ++data->idle_fence_value;

    rsxSetWriteBackendLabel(data->context, PSL1GHT_WAIT_IDLE_LABEL, value);
    rsxFlushBuffer(data->context);

    while (*data->idle_label != value && waited < 20000) {
        usleep(10);
        ++waited;
    }

    if (*data->idle_label != value) {
        SDL_Log("PSL1GHT RSX idle fence timeout during %s: expected=%u actual=%u",
            operation, (unsigned int)value, (unsigned int)*data->idle_label);
        SDL_SetError("PSL1GHT timed out waiting for RSX DMA completion");
        return SDL_FALSE;
    }

    return SDL_TRUE;
}""",
    "flip and RSX fence helpers",
)

# Make partial renderer initialization cleanup aware of allocations as soon
# as they exist, rather than assigning driverdata only after all allocations.
replace_once(
    """    SDL_zerop(data);
    
    deprintf (1, "\\tMem allocated\\n");
    
    // Get a copy of the command buffer
    data->context = ((SDL_DeviceData*) display->device->driverdata)->_CommandBuffer;
    data->current_screen = 0;
    data->first_fb = true;""",
    """    SDL_zerop(data);
    renderer->driverdata = data;

    deprintf (1, "\\tMem allocated\\n");

    // Get a copy of the command buffer and reserve a private RSX idle-fence label.
    data->context = ((SDL_DeviceData*) display->device->driverdata)->_CommandBuffer;
    data->current_screen = 0;
    data->first_fb = true;
    data->idle_label = (volatile u32 *)gcmGetLabelAddress(PSL1GHT_WAIT_IDLE_LABEL);
    if (data->idle_label == NULL) {
        SDL_SetError("Could not acquire the PS3 RSX idle-fence label");
        PSL1GHT_DestroyRenderer(renderer);
        return NULL;
    }
    *data->idle_label = 0;""",
    "renderer initialization and early driverdata assignment",
)

replace_once(
    """        if (!data->textures[i]) {
            deprintf (1, "ERROR\\n");
            PSL1GHT_DestroyRenderer(renderer);
            SDL_OutOfMemory();
            return NULL;
        }

        memset(data->textures[i], 0, displayMode->h * pitch);""",
    """        if (!data->textures[i]) {
            deprintf (1, "ERROR\\n");
            PSL1GHT_DestroyRenderer(renderer);
            SDL_OutOfMemory();
            return NULL;
        }

        data->display_bytes += (size_t)displayMode->h * (size_t)pitch;
        SDL_Log("PSL1GHT display buffer allocated: slot=%d bytes=%u total=%u",
            i, (unsigned int)((size_t)displayMode->h * (size_t)pitch),
            (unsigned int)data->display_bytes);
        memset(data->textures[i], 0, displayMode->h * pitch);""",
    "display buffer accounting",
)

replace_once(
    """    deprintf (1,  "\\tFinished\\n");

    renderer->WindowEvent = PSL1GHT_WindowEvent;""",
    """    deprintf (1, "\\tFinished\\n");

    data->flip_queued = 0;
    data->flip_completed = 0;

    renderer->WindowEvent = PSL1GHT_WindowEvent;""",
    "flip callback registration",
)

replace_once(
    """    renderer->info = PSL1GHT_RenderDriver.info;
    renderer->info.flags = (SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
    renderer->driverdata = data;
    renderer->window = window;""",
    """    renderer->info = PSL1GHT_RenderDriver.info;
    /* Render targets are not implemented by this backend. Do not advertise them. */
    renderer->info.flags = SDL_RENDERER_ACCELERATED;
    renderer->driverdata = data;
    renderer->window = window;""",
    "renderer capability flags",
)

# The backend must fail explicitly for operations that previously returned
# success while doing no work.
replace_once(
    """static int 
PSL1GHT_UpdateTextureYUV(SDL_Renderer * renderer, SDL_Texture * texture,
                     const SDL_Rect * rect,
                     const Uint8 *Yplane, int Ypitch,
                     const Uint8 *Uplane, int Upitch,
                     const Uint8 *Vplane, int Vpitch)
{
    return 0;
}""",
    """static int
PSL1GHT_UpdateTextureYUV(SDL_Renderer * renderer, SDL_Texture * texture,
                     const SDL_Rect * rect,
                     const Uint8 *Yplane, int Ypitch,
                     const Uint8 *Uplane, int Upitch,
                     const Uint8 *Vplane, int Vpitch)
{
    return SDL_Unsupported();
}""",
    "YUV update unsupported path",
)

replace_once(
    """static int 
PSL1GHT_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture)
{
    return 0;
}""",
    """static int
PSL1GHT_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture)
{
    return SDL_Unsupported();
}""",
    "render target unsupported path",
)

replace_once(
    """static int
PSL1GHT_QueueCopyEx(SDL_Renderer * renderer, SDL_RenderCommand *cmd, SDL_Texture * texture,
                const SDL_Rect * srcquad, const SDL_FRect * dstrect,
                const double angle, const SDL_FPoint *center, const SDL_RendererFlip flip)
{
    return 0;
}""",
    """static int
PSL1GHT_QueueCopyEx(SDL_Renderer * renderer, SDL_RenderCommand *cmd, SDL_Texture * texture,
                const SDL_Rect * srcquad, const SDL_FRect * dstrect,
                const double angle, const SDL_FPoint *center, const SDL_RendererFlip flip)
{
    return SDL_Unsupported();
}""",
    "rotated/flipped copy unsupported path",
)

replace_once(
    """            case SDL_RENDERCMD_COPY_EX: {
                break;
            }""",
    """            case SDL_RENDERCMD_COPY_EX: {
                SDL_SetError("PSL1GHT renderer does not support rotated/flipped copies");
                return -1;
            }""",
    "COPY_EX command failure path",
)

replace_once(
    """static void
PSL1GHT_RenderPresent(SDL_Renderer * renderer)
{
    PSL1GHT_RenderData *data = (PSL1GHT_RenderData *) renderer->driverdata;

    if (data->first_fb)
    {
        gcmResetFlipStatus();
    }

    gcmSetFlip(data->context, data->current_screen);
    rsxFlushBuffer(data->context);

    gcmSetWaitFlip(data->context);

    waitFlip();

    data->first_fb = false;

    // Update the flipping chain, if any
    data->current_screen = (data->current_screen + 1) % 2;
}""",
    """static void
PSL1GHT_RenderPresent(SDL_Renderer * renderer)
{
    PSL1GHT_RenderData *data = (PSL1GHT_RenderData *) renderer->driverdata;

    /*
     * Match rsxkit's proven frame_end sequence:
     *   1. Retire the preceding flip on the PPE (never inside the RSX FIFO).
     *   2. Reset flip status before submitting the next buffer.
     *   3. Queue the flip and flush the command buffer.
     * The next frame waits before reusing this buffer.
     */
    if (!data->first_fb && !PSL1GHT_WaitForFreeBuffer(data))
        return;

    gcmResetFlipStatus();

    const s32 flipResult = gcmSetFlip(data->context, data->current_screen);
    if (flipResult != 0) {
        SDL_Log("PSL1GHT gcmSetFlip failed: 0x%08x", (unsigned int)flipResult);
        SDL_SetError("PSL1GHT could not queue display flip");
        return;
    }

    ++data->flip_queued;
    rsxFlushBuffer(data->context);

    data->first_fb = false;
    data->current_screen = (data->current_screen + 1) % 2;
}""",
    "PSL1GHT RenderPresent previous-frame flip completion order",
)

replace_once(
    """static void
PSL1GHT_DestroyTexture(SDL_Renderer * renderer, SDL_Texture * texture)
{
    SDL_Surface *surface = (SDL_Surface *) texture->driverdata;

    if (!surface)
    {
        return;
    }

    // TODO: Wait for the DMA transfer to complete
    rsxFree(surface->pixels);
    SDL_FreeSurface(surface);
}""",
    """static void
PSL1GHT_DestroyTexture(SDL_Renderer * renderer, SDL_Texture * texture)
{
    PSL1GHT_RenderData *data = (PSL1GHT_RenderData *)renderer->driverdata;
    SDL_Surface *surface = (SDL_Surface *)texture->driverdata;
    size_t allocationSize;

    if (!surface)
        return;

    allocationSize = (size_t)surface->pitch * (size_t)surface->h;

    /*
     * SDL_DestroyTexture flushes queued commands that reference this texture.
     * Fence those commands in the RSX backend before returning its memory.
     * On timeout, leak the RSX allocation rather than risk a GPU use-after-free.
     */
    if (!PSL1GHT_WaitForRSXIdle(data, "texture destruction")) {
        SDL_Log("PSL1GHT retaining RSX texture memory after failed DMA fence: ptr=%p bytes=%u",
            surface->pixels, (unsigned int)allocationSize);
        SDL_FreeSurface(surface);
        return;
    }

    if (data != NULL) {
        if (data->texture_bytes >= allocationSize)
            data->texture_bytes -= allocationSize;
        if (data->texture_count > 0)
            --data->texture_count;
        SDL_Log("PSL1GHT texture freed: bytes=%u current=%u peak=%u count=%u",
            (unsigned int)allocationSize, (unsigned int)data->texture_bytes,
            (unsigned int)data->texture_peak_bytes, data->texture_count);
    }

    rsxFree(surface->pixels);
    SDL_FreeSurface(surface);
}""",
    "fenced RSX texture destruction",
)

replace_once(
    """static void
PSL1GHT_DestroyRenderer(SDL_Renderer * renderer)
{
    PSL1GHT_RenderData *data = (PSL1GHT_RenderData *) renderer->driverdata;
    int i;

    deprintf (1, "SDL_PSL1GHT_DestroyRenderer()\\n");

    if (data) {
        for (i = 0; i < SDL_arraysize(data->screens); ++i) {
            if (data->screens[i]) {
               SDL_FreeSurface(data->screens[i]);
            }
            if (data->textures[i]) {
                rsxFree(data->textures[i]);
            }
        }
        SDL_free(data);
    }
    SDL_free(renderer);
}""",
    """static void
PSL1GHT_DestroyRenderer(SDL_Renderer * renderer)
{
    PSL1GHT_RenderData *data = (PSL1GHT_RenderData *) renderer->driverdata;
    SDL_bool rsxMemorySafeToFree = SDL_TRUE;
    int i;

    deprintf (1, "SDL_PSL1GHT_DestroyRenderer()\\n");

    if (data) {
        if (!data->first_fb && !PSL1GHT_WaitForFreeBuffer(data))
            rsxMemorySafeToFree = SDL_FALSE;

        if (data->context != NULL && data->idle_label != NULL
            && !PSL1GHT_WaitForRSXIdle(data, "renderer destruction")) {
            rsxMemorySafeToFree = SDL_FALSE;
        }

        SDL_Log("PSL1GHT renderer teardown: display=%u texture_current=%u texture_peak=%u texture_count=%u",
            (unsigned int)data->display_bytes, (unsigned int)data->texture_bytes,
            (unsigned int)data->texture_peak_bytes, data->texture_count);

        for (i = 0; i < SDL_arraysize(data->screens); ++i) {
            if (data->screens[i])
                SDL_FreeSurface(data->screens[i]);

            if (data->textures[i]) {
                if (rsxMemorySafeToFree) {
                    rsxFree(data->textures[i]);
                } else {
                    SDL_Log("PSL1GHT retaining scanout buffer after failed RSX fence: slot=%d ptr=%p",
                        i, data->textures[i]);
                }
            }
        }

        if (rsxMemorySafeToFree)
            SDL_free(data);
        else
            SDL_Log("PSL1GHT retaining renderer bookkeeping because RSX completion was not confirmed");
    }
    SDL_free(renderer);
}""",
    "fenced renderer teardown and partial-init cleanup",
)

# Replace the upstream texture allocator with guarded allocation and accounting.
old_create = """static int
PSL1GHT_CreateTexture(SDL_Renderer * renderer, SDL_Texture * texture)
{
    int bpp;
    int pitch;
    void *pixels;
    Uint32 Rmask, Gmask, Bmask, Amask;

    if (!SDL_PixelFormatEnumToMasks
        (texture->format, &bpp, &Rmask, &Gmask, &Bmask, &Amask)) {
        SDL_SetError("Unknown texture format");
        return -1;
    }

    // Allocate GFX memory for textures
    pitch = texture->w * SDL_BYTESPERPIXEL(texture->format);
    pixels = rsxMemalign(64, texture->h * pitch);

    texture->driverdata =
        SDL_CreateRGBSurfaceFrom(pixels, texture->w, texture->h, bpp, pitch,
                            Rmask, Gmask, Bmask, Amask);

    SDL_SetSurfaceColorMod(texture->driverdata, texture->r, texture->g,
                           texture->b);
    SDL_SetSurfaceAlphaMod(texture->driverdata, texture->a);
    SDL_SetSurfaceBlendMode(texture->driverdata, texture->blendMode);

    if (!texture->driverdata) {
        return -1;
    }
    return 0;
}"""

new_create = """static int
PSL1GHT_CreateTexture(SDL_Renderer * renderer, SDL_Texture * texture)
{
    PSL1GHT_RenderData *data = (PSL1GHT_RenderData *)renderer->driverdata;
    int bpp;
    int pitch;
    void *pixels;
    Uint32 Rmask, Gmask, Bmask, Amask;
    size_t allocationSize;

    SDL_Log("PSL1GHT texture create begin: %dx%d format=0x%08x",
        texture->w, texture->h, texture->format);
    if (!SDL_PixelFormatEnumToMasks
        (texture->format, &bpp, &Rmask, &Gmask, &Bmask, &Amask)) {
        SDL_SetError("Unknown texture format");
        SDL_Log("PSL1GHT texture create failed: unsupported pixel format");
        return -1;
    }

    pitch = texture->w * SDL_BYTESPERPIXEL(texture->format);
    allocationSize = (size_t)texture->h * (size_t)pitch;
    SDL_Log("PSL1GHT texture allocation begin bytes=%u pitch=%d",
        (unsigned int)allocationSize, pitch);
    pixels = rsxMemalign(64, allocationSize);
    if (!pixels) {
        SDL_Log("PSL1GHT texture allocation failed: rsxMemalign returned NULL");
        SDL_OutOfMemory();
        return -1;
    }

    texture->driverdata =
        SDL_CreateRGBSurfaceFrom(pixels, texture->w, texture->h, bpp, pitch,
                            Rmask, Gmask, Bmask, Amask);
    if (!texture->driverdata) {
        SDL_Log("PSL1GHT texture surface creation failed: %s", SDL_GetError());
        rsxFree(pixels);
        return -1;
    }

    SDL_SetSurfaceColorMod(texture->driverdata, texture->r, texture->g,
                           texture->b);
    SDL_SetSurfaceAlphaMod(texture->driverdata, texture->a);
    SDL_SetSurfaceBlendMode(texture->driverdata, texture->blendMode);

    if (data != NULL) {
        data->texture_bytes += allocationSize;
        ++data->texture_count;
        if (data->texture_bytes > data->texture_peak_bytes)
            data->texture_peak_bytes = data->texture_bytes;
        if (data->texture_count > data->texture_peak_count)
            data->texture_peak_count = data->texture_count;
    }

    SDL_Log("PSL1GHT texture create complete: ptr=%p bytes=%u current=%u peak=%u count=%u",
        pixels, (unsigned int)allocationSize,
        (unsigned int)(data != NULL ? data->texture_bytes : allocationSize),
        (unsigned int)(data != NULL ? data->texture_peak_bytes : allocationSize),
        data != NULL ? data->texture_count : 1);
    return 0;
}"""

replace_once(old_create, new_create, "RSX texture allocation guard and accounting")

# The generated source must not retain any of the old unsafe synchronization.
for required in (
    "PSL1GHT_WaitForFreeBuffer(data)",
    "gcmGetFlipStatus()",
    "gcmGetControlRegister()",
    "gcmResetFlipStatus()",
    "rsxSetWriteBackendLabel(data->context, PSL1GHT_WAIT_IDLE_LABEL, value)",
    "PSL1GHT timed out waiting for display flip completion",
    "PSL1GHT timed out waiting for RSX DMA completion",
    "texture_peak_bytes",
    "SDL_Unsupported()",
):
    if required not in source:
        raise SystemExit(f"error: required PS3 renderer change missing: {required}")

if "waitFlip()" in source or "gcmSetWaitFlip(" in source or "gcmSetFlipHandler(" in source:
    raise SystemExit("error: obsolete RSX flip callback / FIFO wait remains in the patched renderer")

path.write_text(source, encoding="utf-8")
print("Applied rsxkit-style PPE flip polling, RSX backend DMA fences, renderer capability fixes, and RSX memory accounting")
