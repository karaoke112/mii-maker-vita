#include "gpu_renderer.hpp"

GpuRenderer& GpuRenderer::instance()
{
    static GpuRenderer g;
    return g;
}

#if !defined(__PSV__)

// Desktop builds: the CPU renderer only.
bool GpuRenderer::init(const mii3d_res*, const mii3d_res*, const std::string&)
{
    why = "GPU rendering is only available on the Vita";
    return false;
}
int GpuRenderer::render(const void*, const mii_charinfo_t&, int, float, float, float, bool, uint32_t) { return -1; }
void GpuRenderer::release(const void*) { }

#else

#include <borealis.hpp>
#include <mii3d/mii3d.h>
#include <nanovg_gxm.h>
#include <nanovg_gxm_utils.h>
#include <psp2/gxm.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/shacccg.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sys/stat.h>
#include <vector>

// ---- shaders -------------------------------------------------------------
// The vertex shader passes libmii3d's clip-space vertices through; the
// fragment shader is the CPU rasterizer's shade_fragment + lighting.

static const char kVertexShader[] = R"(
void main(
    float4 aPos : POSITION,
    float2 aUv : TEXCOORD0,
    float4 aLight : TEXCOORD1,
    float3 aRim : TEXCOORD2,
    uniform float depthBias,
    out float4 oPos : POSITION,
    out float2 oUv : TEXCOORD0,
    out float4 oLight : TEXCOORD1,
    out float3 oRim : TEXCOORD2)
{
    oPos = float4(aPos.x, aPos.y, aPos.z - depthBias * aPos.w, aPos.w);
    oUv = aUv;
    oLight = aLight;
    oRim = aRim;
}
)";

// mode: x = modulation (mii3d_modulate_t), y = textured, z = mirror u
// shading: x = lit, y = faceline, z = SSS blend
static const char kFragmentShader[] = R"(
float4 main(
    float2 vUv : TEXCOORD0,
    float4 vLight : TEXCOORD1,
    float3 vRim : TEXCOORD2,
    uniform sampler2D tex : TEXUNIT0,
    uniform float4 base,
    uniform float4 c0,
    uniform float4 c1,
    uniform float4 c2,
    uniform float4 mode,
    uniform float4 shading) : COLOR
{
    float4 c = base;
    if (mode.y > 0.5) {
        float2 uv = vUv;
        if (mode.z > 0.5) {
            float m = fmod(abs(uv.x), 2.0);
            uv.x = m > 1.0 ? 2.0 - m : m;
        }
        float4 t = tex2D(tex, saturate(uv));
        float4 m4;
        if (mode.x < 1.5)
            m4 = t;
        else if (mode.x < 2.5)
            m4 = float4(t.r * c0.rgb + t.g * c1.rgb + t.b * c2.rgb, t.a);
        else if (mode.x < 3.5)
            m4 = float4(c0.rgb, t.r);
        else if (mode.x < 4.5)
            m4 = float4(t.r * c0.rgb, t.g);
        else if (mode.x < 5.5)
            m4 = float4(t.r * c0.rgb, 1.0);
        else
            m4 = float4(t.g * c0.rgb, t.r);
        if (base.a > 0.0)
            c = float4(base.rgb + (m4.rgb - base.rgb) * m4.a, base.a + (1.0 - base.a) * m4.a);
        else
            c = m4;
    }
    if (shading.y > 0.5) {
        float k = 1.0 - c.a * shading.z;
        c.rgb = pow(saturate(c.rgb), 2.2) * vLight.x + vLight.yzw * k + vRim;
        c.a = 1.0;
    } else if (shading.x > 0.5) {
        if (c.a <= 0.004)
            discard;
        c.rgb = pow(saturate(c.rgb), 2.2) * vLight.x + vLight.yzw + vRim;
    } else if (c.a <= 0.004) {
        discard;
    }
    if (shading.x > 0.5 || shading.y > 0.5)
        c.rgb = pow(saturate(c.rgb), 1.0 / 2.2);
    return c;
}
)";

// ---- the shader compiler (libshacccg.suprx) -------------------------------

static SceShaccCgSourceFile gSource;

static SceShaccCgSourceFile* openSource(const char*, const SceShaccCgSourceLocation*, const SceShaccCgCompileOptions*,
    const char**)
{
    return &gSource;
}

namespace {

struct Log {
    FILE* f = nullptr;
    void open(const std::string& path)
    {
        f = std::fopen(path.c_str(), "w");
    }
    void operator()(const char* fmt, ...)
    {
        va_list ap;
        va_start(ap, fmt);
        char buf[512];
        std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        brls::Logger::info("gpu: {}", buf);
        if (f) {
            std::fprintf(f, "%s\n", buf);
            std::fflush(f);
        }
    }
};

struct Texture {
    NVGXMtexture* tex = nullptr;
    int width = 0, height = 0;
};

struct Target {
    int size = 0;
    NVGXMtexture* tex = nullptr;
    SceGxmColorSurface surface;
    int nvgImage = -1;
};

struct SizeResources {
    SceGxmRenderTarget* rt = nullptr;
    SceUID depthUid = 0;
    void* depthAddr = nullptr;
    SceGxmDepthStencilSurface depth;
};

// Vertex layout from mii3d_draw_t: x y z w, u v, hl, add rgb, rim rgb.
constexpr int kStride = 15 * 4;
constexpr size_t kRingBytes = 6u << 20;
constexpr size_t kMaxCachedTextures = 96;

} // namespace

struct GpuRenderer::Impl {
    Log log;
    SceGxmContext* ctx = nullptr;
    mii3d_renderer_t* renderer = nullptr;
    std::vector<uint8_t> vertGxp, fragGxp;
    NVGXMshaderProgram prog;
    const SceGxmProgramParameter *pDepthBias = nullptr, *pBase = nullptr, *pC0 = nullptr, *pC1 = nullptr,
                                 *pC2 = nullptr, *pMode = nullptr, *pShading = nullptr;
    SceUID ringUid = 0;
    uint8_t* ring = nullptr;
    size_t ringUsed = 0;
    Texture white;
    std::map<uint32_t, Texture> textures;           // by mii3d_image_t serial
    std::map<const mii3d_image_t*, Texture> dynamic; // composed face textures
    std::map<const void*, Target> targets;
    std::vector<Target> released;
    std::map<int, SizeResources> sizes;
    bool drawFailed = false;

    bool compile(const char* src, SceShaccCgTargetProfile profile, std::vector<uint8_t>* out);
    bool createPrograms();
    bool upload(Texture* t, const mii3d_image_t* img);
    const SceGxmTexture* textureFor(const mii3d_image_t* img, bool dynamicImage);
    SizeResources* resourcesFor(int size);
    Target* targetFor(const void* owner, int size);
    void* ringAlloc(size_t bytes);
    void draw(const mii3d_draw_t* d);
    void freeTarget(Target& t);
};

bool GpuRenderer::Impl::compile(const char* src, SceShaccCgTargetProfile profile, std::vector<uint8_t>* out)
{
    gSource.fileName = "<mii>";
    gSource.text = src;
    gSource.size = (SceUInt32)std::strlen(src);

    SceShaccCgCallbackList callbacks;
    sceShaccCgInitializeCallbackList(&callbacks, SCE_SHACCCG_TRIVIAL);
    callbacks.openFile = openSource;

    SceShaccCgCompileOptions options;
    sceShaccCgInitializeCompileOptions(&options);
    options.mainSourceFile = gSource.fileName;
    options.targetProfile = profile;
    options.entryFunctionName = "main";
    options.locale = SCE_SHACCCG_ENGLISH;
    options.useFx = 1;
    options.optimizationLevel = 3;
    options.useFastmath = 1;
    options.useFastprecision = 1;
    options.useFastint = 1;
    options.warningLevel = 1;

    const SceShaccCgCompileOutput* result = sceShaccCgCompileProgram(&options, &callbacks, 0);
    if (!result)
        return false;
    for (int i = 0; i < result->diagnosticCount; i++) {
        const SceShaccCgDiagnosticMessage& m = result->diagnostics[i];
        log("shader %s: line %u: %s", profile == SCE_SHACCCG_PROFILE_VP ? "vertex" : "fragment",
            m.location ? m.location->lineNumber : 0, m.message ? m.message : "");
    }
    const bool good = result->programData && result->programSize > 0;
    if (good)
        out->assign(result->programData, result->programData + result->programSize);
    sceShaccCgDestroyCompileOutput(result);
    return good;
}

bool GpuRenderer::Impl::createPrograms()
{
    std::memset(&prog, 0, sizeof(prog));
    if (!gxmCreateShader(&prog, "mii", (const char*)vertGxp.data(), (const char*)fragGxp.data())) {
        log("could not register the shaders");
        return false;
    }
    const SceGxmProgram* v = prog.vert_gxp;
    const SceGxmProgram* f = prog.frag_gxp;
    const SceGxmProgramParameter* aPos = sceGxmProgramFindParameterByName(v, "aPos");
    const SceGxmProgramParameter* aUv = sceGxmProgramFindParameterByName(v, "aUv");
    const SceGxmProgramParameter* aLight = sceGxmProgramFindParameterByName(v, "aLight");
    const SceGxmProgramParameter* aRim = sceGxmProgramFindParameterByName(v, "aRim");
    pDepthBias = sceGxmProgramFindParameterByName(v, "depthBias");
    pBase = sceGxmProgramFindParameterByName(f, "base");
    pC0 = sceGxmProgramFindParameterByName(f, "c0");
    pC1 = sceGxmProgramFindParameterByName(f, "c1");
    pC2 = sceGxmProgramFindParameterByName(f, "c2");
    pMode = sceGxmProgramFindParameterByName(f, "mode");
    pShading = sceGxmProgramFindParameterByName(f, "shading");
    if (!aPos || !aUv || !aLight || !aRim) {
        log("vertex attributes missing");
        return false;
    }
    SceGxmVertexAttribute attrs[4];
    const struct {
        const SceGxmProgramParameter* p;
        int offset, count;
    } layout[4] = { { aPos, 0, 4 }, { aUv, 16, 2 }, { aLight, 24, 4 }, { aRim, 40, 3 } };
    for (int i = 0; i < 4; i++) {
        attrs[i].streamIndex = 0;
        attrs[i].offset = (uint16_t)layout[i].offset;
        attrs[i].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
        attrs[i].componentCount = (uint8_t)layout[i].count;
        attrs[i].regIndex = (uint16_t)sceGxmProgramParameterGetResourceIndex(layout[i].p);
    }
    SceGxmVertexStream stream;
    stream.stride = kStride;
    stream.indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;
    int err = gxmCreateVertexProgram(prog.vert_id, attrs, 4, &stream, 1, &prog.vert);
    if (err < 0) {
        log("vertex program: 0x%08X", err);
        return false;
    }
    // non-premultiplied "over", like the CPU rasterizer
    SceGxmBlendInfo blend;
    std::memset(&blend, 0, sizeof(blend));
    blend.colorMask = SCE_GXM_COLOR_MASK_ALL;
    blend.colorFunc = SCE_GXM_BLEND_FUNC_ADD;
    blend.alphaFunc = SCE_GXM_BLEND_FUNC_ADD;
    blend.colorSrc = SCE_GXM_BLEND_FACTOR_SRC_ALPHA;
    blend.colorDst = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaSrc = SCE_GXM_BLEND_FACTOR_ONE;
    blend.alphaDst = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    err = gxmCreateFragmentProgram(prog.frag_id, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, &blend, prog.vert_gxp,
        &prog.frag);
    if (err < 0) {
        log("fragment program: 0x%08X", err);
        return false;
    }
    return true;
}

// Copies an image into a GPU texture as RGBA (missing channels: g, b = 0,
// a = 1, like the CPU rasterizer's fetch).
bool GpuRenderer::Impl::upload(Texture* t, const mii3d_image_t* img)
{
    if (!t->tex || t->width != img->width || t->height != img->height) {
        if (t->tex)
            gxmDeleteTexture(t->tex);
        t->tex = gxmCreateTexture(img->width, img->height, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, nullptr);
        if (!t->tex)
            return false;
        t->width = img->width;
        t->height = img->height;
        sceGxmTextureSetMinFilter(&t->tex->tex, SCE_GXM_TEXTURE_FILTER_LINEAR);
        sceGxmTextureSetMagFilter(&t->tex->tex, SCE_GXM_TEXTURE_FILTER_LINEAR);
        sceGxmTextureSetUAddrMode(&t->tex->tex, SCE_GXM_TEXTURE_ADDR_CLAMP);
        sceGxmTextureSetVAddrMode(&t->tex->tex, SCE_GXM_TEXTURE_ADDR_CLAMP);
    }
    const int stride = ((img->width + 7) & ~7) * 4;
    const int ch = img->channels;
    for (int y = 0; y < img->height; y++) {
        uint8_t* dst = t->tex->data + (size_t)y * stride;
        const uint8_t* src = img->pixels + (size_t)y * img->width * ch;
        for (int x = 0; x < img->width; x++, dst += 4, src += ch) {
            dst[0] = src[0];
            dst[1] = ch >= 2 ? src[1] : 0;
            dst[2] = ch >= 4 ? src[2] : 0;
            dst[3] = ch >= 4 ? src[3] : 255;
        }
    }
    return true;
}

const SceGxmTexture* GpuRenderer::Impl::textureFor(const mii3d_image_t* img, bool dynamicImage)
{
    if (!img || !img->pixels)
        return &white.tex->tex;
    if (dynamicImage) {
        Texture& t = dynamic[img];
        return upload(&t, img) ? &t.tex->tex : &white.tex->tex;
    }
    auto it = textures.find(img->serial);
    if (it != textures.end())
        return &it->second.tex->tex;
    if (textures.size() >= kMaxCachedTextures) {
        // simple: start over (safe: the GPU is idle between renders)
        for (auto& kv : textures)
            gxmDeleteTexture(kv.second.tex);
        textures.clear();
    }
    Texture t;
    if (!upload(&t, img))
        return &white.tex->tex;
    textures[img->serial] = t;
    return &t.tex->tex;
}

SizeResources* GpuRenderer::Impl::resourcesFor(int size)
{
    auto it = sizes.find(size);
    if (it != sizes.end())
        return &it->second;
    SizeResources r;
    SceGxmRenderTargetParams params;
    std::memset(&params, 0, sizeof(params));
    params.width = size;
    params.height = size;
    params.scenesPerFrame = 1;
    // must match the fragment program, which uses borealis' setting (4x)
    params.multisampleMode = SCE_GXM_MULTISAMPLE_4X;
    params.driverMemBlock = -1;
    int err = sceGxmCreateRenderTarget(&params, &r.rt);
    if (err < 0) {
        log("render target %d: 0x%08X", size, err);
        return nullptr;
    }
    // 4x MSAA depth/stencil: twice as wide and tall in samples
    const unsigned w = (size + SCE_GXM_TILE_SIZEX - 1) & ~(SCE_GXM_TILE_SIZEX - 1);
    const unsigned h = (size + SCE_GXM_TILE_SIZEY - 1) & ~(SCE_GXM_TILE_SIZEY - 1);
    r.depthAddr = gpu_alloc_map(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, SCE_GXM_MEMORY_ATTRIB_RW, 4 * w * h * 4,
        &r.depthUid);
    if (!r.depthAddr) {
        log("depth buffer %d: out of memory", size);
        return nullptr;
    }
    sceGxmDepthStencilSurfaceInit(&r.depth, SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24, SCE_GXM_DEPTH_STENCIL_SURFACE_TILED,
        w * 2, r.depthAddr, nullptr);
    // each scene starts with depth "far" (not loaded from memory)
    sceGxmDepthStencilSurfaceSetBackgroundDepth(&r.depth, 1.0f);
    sizes[size] = r;
    return &sizes[size];
}

Target* GpuRenderer::Impl::targetFor(const void* owner, int size)
{
    Target& t = targets[owner];
    if (t.tex && t.size == size)
        return &t;
    if (t.tex) {
        released.push_back(t);
        t = Target();
    }
    t.tex = gxmCreateTexture(size, size, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, nullptr);
    if (!t.tex) {
        targets.erase(owner);
        return nullptr;
    }
    t.size = size;
    const int stridePx = (size + 7) & ~7;
    int err = sceGxmColorSurfaceInit(&t.surface, SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR, SCE_GXM_COLOR_SURFACE_LINEAR,
        SCE_GXM_COLOR_SURFACE_SCALE_MSAA_DOWNSCALE, SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, size, size, stridePx,
        t.tex->data);
    if (err < 0) {
        log("color surface: 0x%08X", err);
        gxmDeleteTexture(t.tex);
        targets.erase(owner);
        return nullptr;
    }
    t.nvgImage = nvgxmCreateImageFromHandle(brls::Application::getNVGContext(), &t.tex->tex);
    return &t;
}

void* GpuRenderer::Impl::ringAlloc(size_t bytes)
{
    bytes = (bytes + 15) & ~(size_t)15;
    if (ringUsed + bytes > kRingBytes)
        return nullptr;
    void* p = ring + ringUsed;
    ringUsed += bytes;
    return p;
}

void GpuRenderer::Impl::draw(const mii3d_draw_t* d)
{
    void* verts = ringAlloc((size_t)d->vertex_count * kStride);
    void* idx = ringAlloc((size_t)d->index_count * 2);
    if (!verts || !idx) {
        drawFailed = true;
        return;
    }
    std::memcpy(verts, d->vertices, (size_t)d->vertex_count * kStride);
    std::memcpy(idx, d->indices, (size_t)d->index_count * 2);

    sceGxmSetFrontDepthFunc(ctx, d->depth_test ? SCE_GXM_DEPTH_FUNC_LESS_EQUAL : SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(ctx, d->depth_write ? SCE_GXM_DEPTH_WRITE_ENABLED : SCE_GXM_DEPTH_WRITE_DISABLED);

    void* vbuf;
    sceGxmReserveVertexDefaultUniformBuffer(ctx, &vbuf);
    const float bias = d->depth_bias;
    if (pDepthBias)
        sceGxmSetUniformDataF(vbuf, pDepthBias, 0, 1, &bias);

    void* fbuf;
    sceGxmReserveFragmentDefaultUniformBuffer(ctx, &fbuf);
    const bool textured = d->texture && d->mode != MII3D_MOD_NONE;
    const float mode[4] = { (float)d->mode, textured ? 1.0f : 0.0f, d->mirror_u ? 1.0f : 0.0f, 0.0f };
    const float shading[4] = { d->lit ? 1.0f : 0.0f, d->faceline ? 1.0f : 0.0f, d->sss_blend, 0.0f };
    if (pBase)
        sceGxmSetUniformDataF(fbuf, pBase, 0, 4, d->base);
    if (pC0)
        sceGxmSetUniformDataF(fbuf, pC0, 0, 4, d->c[0]);
    if (pC1)
        sceGxmSetUniformDataF(fbuf, pC1, 0, 4, d->c[1]);
    if (pC2)
        sceGxmSetUniformDataF(fbuf, pC2, 0, 4, d->c[2]);
    if (pMode)
        sceGxmSetUniformDataF(fbuf, pMode, 0, 4, mode);
    if (pShading)
        sceGxmSetUniformDataF(fbuf, pShading, 0, 4, shading);

    sceGxmSetFragmentTexture(ctx, 0, textureFor(textured ? d->texture : nullptr, d->texture_dynamic));
    sceGxmSetVertexStream(ctx, 0, verts);
    sceGxmDraw(ctx, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U16, idx, d->index_count);
}

void GpuRenderer::Impl::freeTarget(Target& t)
{
    if (t.nvgImage > 0)
        nvgDeleteImage(brls::Application::getNVGContext(), t.nvgImage); // NODELETE: frees only the handle
    if (t.tex)
        gxmDeleteTexture(t.tex);
    t = Target();
}

static void drawCallback(void* user, const mii3d_draw_t* d)
{
    static_cast<GpuRenderer::Impl*>(user)->draw(d);
}

bool GpuRenderer::init(const mii3d_res* face, const mii3d_res* body, const std::string& logPath)
{
    if (ok || impl)
        return ok;
    impl = new Impl();
    Log& log = impl->log;
    log.open(logPath);

    // the shader compiler from the console, as ShaRKF00D installs it
    SceUID module = -1;
    std::string found;
    for (const char* path : { "ur0:data/libshacccg.suprx", "ur0:data/external/libshacccg.suprx",
             "ux0:data/mii-maker-vita/dat/libshacccg.suprx" }) {
        struct stat st;
        if (stat(path, &st) != 0)
            continue;
        module = sceKernelLoadStartModule(path, 0, nullptr, 0, nullptr, nullptr);
        log("load %s: 0x%08X", path, (unsigned)module);
        if (module >= 0) {
            found = path;
            break;
        }
    }
    if (module < 0) {
        why = "GPU rendering needs libshacccg.suprx (install it with ShaRKF00D)";
        log("%s", why.c_str());
        return false;
    }
    sceShaccCgSetDefaultAllocator(malloc, free);
    if (!impl->compile(kVertexShader, SCE_SHACCCG_PROFILE_VP, &impl->vertGxp) ||
        !impl->compile(kFragmentShader, SCE_SHACCCG_PROFILE_FP, &impl->fragGxp)) {
        why = "GPU shaders did not compile (see ux0:data/mii-maker-vita/gpu.log)";
        log("%s", why.c_str());
        return false;
    }
    log("shaders compiled: vertex %u bytes, fragment %u bytes", (unsigned)impl->vertGxp.size(),
        (unsigned)impl->fragGxp.size());

    NVGXMwindow* window = gxmGetWindow();
    if (!window || !window->context) {
        why = "no GXM context";
        log("%s", why.c_str());
        return false;
    }
    impl->ctx = window->context;
    if (!impl->createPrograms()) {
        why = "GPU programs could not be created (see gpu.log)";
        return false;
    }
    impl->ring = (uint8_t*)gpu_alloc_map(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, SCE_GXM_MEMORY_ATTRIB_READ, kRingBytes,
        &impl->ringUid);
    uint8_t w[4 * 8 * 8];
    std::memset(w, 255, sizeof(w));
    impl->white.tex = gxmCreateTexture(8, 8, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, w);
    if (!impl->ring || !impl->white.tex) {
        why = "out of GPU memory";
        log("%s", why.c_str());
        return false;
    }
    if (mii3d_renderer_create(&impl->renderer, face) != MII3D_OK) {
        why = "renderer";
        return false;
    }
    if (body)
        mii3d_renderer_set_body(impl->renderer, body);
    ok = true;
    why = "GPU (shader compiler: " + found + ")";
    log("ready");
    return true;
}

int GpuRenderer::render(const void* owner, const mii_charinfo_t& mii, int size, float yaw, float zoom, float centerY,
    bool fullBody, uint32_t background)
{
    if (!ok)
        return -1;
    Impl& g = *impl;

    size = (size + 7) & ~7;
    SizeResources* sr = g.resourcesFor(size);
    Target* t = sr ? g.targetFor(owner, size) : nullptr;
    if (!t) {
        ok = false;
        why = "GPU render target failed (see gpu.log)";
        return -1;
    }

    mii3d_view_t view;
    mii3d_view_init_default(&view);
    view.yaw_deg = yaw;
    view.zoom = zoom;
    view.center_y = centerY;
    view.full_body = fullBody;

    g.ringUsed = 0;
    g.drawFailed = false;
    int err = sceGxmBeginScene(g.ctx, 0, sr->rt, nullptr, nullptr, nullptr, &t->surface, &sr->depth);
    if (err < 0) {
        g.log("begin scene: 0x%08X", err);
        ok = false;
        why = "GPU scene failed (see gpu.log)";
        return -1;
    }
    gxmClearColor(((background >> 16) & 0xFF) / 255.0f, ((background >> 8) & 0xFF) / 255.0f,
        (background & 0xFF) / 255.0f, 1.0f);
    gxmClear();
    sceGxmSetVertexProgram(g.ctx, g.prog.vert);
    sceGxmSetFragmentProgram(g.ctx, g.prog.frag);
    sceGxmSetTwoSidedEnable(g.ctx, SCE_GXM_TWO_SIDED_DISABLED);
    sceGxmSetCullMode(g.ctx, SCE_GXM_CULL_NONE);
    sceGxmSetFrontStencilFunc(g.ctx, SCE_GXM_STENCIL_FUNC_ALWAYS, SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP,
        SCE_GXM_STENCIL_OP_KEEP, 0, 0);
    mii3d_result_t r = mii3d_render_head_draws(g.renderer, &mii, &view, size, size, drawCallback, &g);
    sceGxmEndScene(g.ctx, nullptr, nullptr);
    // leave the depth state as borealis expects it (GXM's defaults)
    sceGxmSetFrontDepthFunc(g.ctx, SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(g.ctx, SCE_GXM_DEPTH_WRITE_ENABLED);
    sceGxmFinish(g.ctx);
    // The GPU is idle now, so textures of previews that went away can go
    // (a frame drawn before they went may have still been using them).
    for (Target& old : g.released)
        g.freeTarget(old);
    g.released.clear();
    if (r != MII3D_OK || g.drawFailed) {
        g.log("render: %s%s", mii3d_result_string(r), g.drawFailed ? " (vertex buffer full)" : "");
        return -1;
    }
    return t->nvgImage;
}

void GpuRenderer::release(const void* owner)
{
    if (!impl)
        return;
    auto it = impl->targets.find(owner);
    if (it == impl->targets.end())
        return;
    impl->released.push_back(it->second);
    impl->targets.erase(it);
}

#endif
