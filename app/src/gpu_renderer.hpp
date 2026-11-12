#pragma once

#include <mii/mii.h>

#include <string>

struct mii3d_res;

// Draws Mii previews with the Vita's GPU (GXM), using the shader compiler
// from the console (libshacccg.suprx, installed by ShaRKF00D) to build its
// two shaders when the app starts. libmii3d still does everything but
// filling pixels: it hands over transformed, lit vertices and materials
// (mii3d_render_head_draws), and the fragment shader repeats the CPU
// rasterizer's per-pixel step.
//
// Each owner (a preview) gets its own render-target texture, shown through
// NanoVG. Rendering happens on the main thread between frames (pump()).
// On other platforms, or without the compiler, ready() is false and the
// CPU renderer is used.
class GpuRenderer
{
  public:
    static GpuRenderer& instance();

    // Sets up the GPU path; on failure reason() says why (also logged to
    // `logPath`).
    bool init(const mii3d_res* face, const mii3d_res* body, const std::string& logPath);
    bool ready() const { return ok; }
    const std::string& reason() const { return why; }

    // Draws `mii` into `owner`'s texture (square, `size` px) on an opaque
    // `background` (0xRRGGBB). Returns a NanoVG image id (owned here), or -1
    // on failure. Main thread, outside a frame.
    int render(const void* owner, const mii_charinfo_t& mii, int size, float yaw, float pitch, float zoom, float centerY,
        bool fullBody, uint32_t background);
    // The owner is gone: its texture is freed after the next render.
    void release(const void* owner);
    // The NanoVG image of the owner's latest render, or -1.
    int imageOf(const void* owner);

    struct Impl; // GXM state (Vita builds only)

  private:
    GpuRenderer() = default;
    bool ok = false;
    std::string why = "GPU renderer not started";
    Impl* impl = nullptr;
};
