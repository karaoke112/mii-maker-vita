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
#include <functional>
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
constexpr int kFloats = 15;
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

    int stride = kFloats * 4; // bytes per vertex in GPU memory (60, or 64 padded)
    bool useFx = true;

    bool compile(const char* src, SceShaccCgTargetProfile profile, std::vector<uint8_t>* out);
    bool createPrograms();
    void destroyPrograms();
    bool scene(Target* t, SizeResources* sr, uint32_t background, const std::function<void()>& body);
    bool selfTest();
    const uint8_t* pixel(const Target* t, int x, int y) const
    {
        return t->tex->data + (size_t)y * ((t->size + 7) & ~7) * 4 + (size_t)x * 4;
    }
    bool upload(Texture* t, const mii3d_image_t* img);
    const SceGxmTexture* textureFor(const mii3d_image_t* img, bool dynamicImage);
    SizeResources* resourcesFor(int size);
    Target* targetFor(const void* owner, int size);
    void* ringAlloc(size_t bytes);
    void draw(const mii3d_draw_t* d);
    void freeTarget(Target& t);
    void release(const void* owner)
    {
        auto it = targets.find(owner);
        if (it == targets.end())
            return;
        released.push_back(it->second);
        targets.erase(it);
    }
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
    options.useFx = useFx ? 1 : 0;
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
    for (const SceGxmProgramParameter* q : { aPos, aUv, aLight, aRim, pDepthBias, pBase, pMode, pShading }) {
        if (!q)
            continue;
        log("param %s: category %d, type %d, components %u, array %u, resource %u",
            sceGxmProgramParameterGetName(q), (int)sceGxmProgramParameterGetCategory(q),
            (int)sceGxmProgramParameterGetType(q), sceGxmProgramParameterGetComponentCount(q),
            sceGxmProgramParameterGetArraySize(q), sceGxmProgramParameterGetResourceIndex(q));
    }
    log("default uniform buffer: vertex %u, fragment %u bytes", sceGxmProgramGetDefaultUniformBufferSize(v),
        sceGxmProgramGetDefaultUniformBufferSize(f));
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
    stream.stride = (uint16_t)stride;
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
    void* verts = ringAlloc((size_t)d->vertex_count * stride);
    void* idx = ringAlloc((size_t)d->index_count * 2);
    if (!verts || !idx) {
        drawFailed = true;
        return;
    }
    if (stride == kFloats * 4) {
        std::memcpy(verts, d->vertices, (size_t)d->vertex_count * stride);
    } else {
        float* out = (float*)verts;
        for (int v = 0; v < d->vertex_count; v++, out += stride / 4) {
            std::memcpy(out, d->vertices + (size_t)v * kFloats, kFloats * 4);
            out[kFloats] = 0.0f;
        }
    }
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

void GpuRenderer::Impl::destroyPrograms()
{
    gxmDeleteShader(&prog);
    std::memset(&prog, 0, sizeof(prog));
}

// One offscreen scene into `t`: clear to `background`, run `body` (draws),
// wait for the GPU.
bool GpuRenderer::Impl::scene(Target* t, SizeResources* sr, uint32_t background, const std::function<void()>& body)
{
    ringUsed = 0;
    drawFailed = false;
    int err = sceGxmBeginScene(ctx, 0, sr->rt, nullptr, nullptr, nullptr, &t->surface, &sr->depth);
    if (err < 0) {
        log("begin scene: 0x%08X", err);
        return false;
    }
    gxmClearColor(((background >> 16) & 0xFF) / 255.0f, ((background >> 8) & 0xFF) / 255.0f,
        (background & 0xFF) / 255.0f, 1.0f);
    gxmClear();
    sceGxmSetVertexProgram(ctx, prog.vert);
    sceGxmSetFragmentProgram(ctx, prog.frag);
    sceGxmSetTwoSidedEnable(ctx, SCE_GXM_TWO_SIDED_DISABLED);
    sceGxmSetCullMode(ctx, SCE_GXM_CULL_NONE);
    sceGxmSetFrontStencilFunc(ctx, SCE_GXM_STENCIL_FUNC_ALWAYS, SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP,
        SCE_GXM_STENCIL_OP_KEEP, 0, 0);
    body();
    sceGxmEndScene(ctx, nullptr, nullptr);
    // leave the depth state as borealis expects it (GXM's defaults)
    sceGxmSetFrontDepthFunc(ctx, SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(ctx, SCE_GXM_DEPTH_WRITE_ENABLED);
    sceGxmFinish(ctx);
    // The GPU is idle now, so textures of previews that went away can go
    // (a frame drawn before they went may have still been using them).
    for (Target& old : released)
        freeTarget(old);
    released.clear();
    return !drawFailed;
}

// Draws known shapes and a whole Mii, reads the pixels back and compares
// them with what the CPU renderer draws. Logs what it sees, so a failure
// says which step went wrong.
bool GpuRenderer::Impl::selfTest()
{
    const int N = 64;
    SizeResources* sr = resourcesFor(N);
    Target* t = sr ? targetFor((const void*)this, N) : nullptr;
    if (!t)
        return false;
    auto px = [&](int x, int y) {
        const uint8_t* p = pixel(t, x, y);
        char buf[48];
        std::snprintf(buf, sizeof(buf), "(%d,%d,%d,%d)", p[0], p[1], p[2], p[3]);
        return std::string(buf);
    };
    auto near = [&](int x, int y, int r, int g, int b) {
        const uint8_t* p = pixel(t, x, y);
        return std::abs(p[0] - r) <= 12 && std::abs(p[1] - g) <= 12 && std::abs(p[2] - b) <= 12;
    };
    // A triangle in the top-left quarter: (-1,1), (0,1), (-1,0) in clip space.
    float tri[3 * kFloats];
    std::memset(tri, 0, sizeof(tri));
    const float corners[3][2] = { { -1, 1 }, { 0, 1 }, { -1, 0 } };
    for (int i = 0; i < 3; i++) {
        float* v = tri + i * kFloats;
        v[0] = corners[i][0];
        v[1] = corners[i][1];
        v[2] = 0.5f;
        v[3] = 1.0f;
        v[6] = 1.0f; // hl
    }
    const uint16_t idx[3] = { 0, 1, 2 };
    auto flat = [&](float r, float g, float b, int lit) {
        mii3d_draw_t d;
        std::memset(&d, 0, sizeof(d));
        d.vertices = tri;
        d.vertex_count = 3;
        d.indices = idx;
        d.index_count = 3;
        d.mode = MII3D_MOD_NONE;
        d.base[0] = r;
        d.base[1] = g;
        d.base[2] = b;
        d.base[3] = 1.0f;
        d.lit = lit;
        return d;
    };

    // 1: geometry and a constant color
    mii3d_draw_t d1 = flat(1.0f, 0.0f, 0.0f, 0);
    if (!scene(t, sr, 0x0000FF, [&] { draw(&d1); }))
        return false;
    log("test 1 (red triangle at top left on blue): top-left %s, bottom-left %s, top-right %s", px(4, 4).c_str(),
        px(4, N - 5).c_str(), px(N - 5, 4).c_str());
    const bool upright = near(4, 4, 255, 0, 0) && near(4, N - 5, 0, 0, 255) && near(N - 5, 4, 0, 0, 255);
    const bool flipped = near(4, N - 5, 255, 0, 0) && near(4, 4, 0, 0, 255);
    if (!upright) {
        log("test 1 failed%s", flipped ? " (the image is upside down)" : "");
        return false;
    }
    // 2: per-vertex lighting (hl = 1, nothing added: the same color back)
    mii3d_draw_t d2 = flat(0.0f, 0.5f, 1.0f, 1);
    if (!scene(t, sr, 0x000000, [&] { draw(&d2); }))
        return false;
    log("test 2 (lit (0,128,255)): %s", px(4, 4).c_str());
    if (!near(4, 4, 0, 128, 255)) {
        log("test 2 failed");
        return false;
    }
    // 3: a whole Mii against the CPU renderer
    const int M = 128;
    SizeResources* sr2 = resourcesFor(M);
    Target* t2 = sr2 ? targetFor((const void*)&textures, M) : nullptr;
    if (!t2)
        return false;
    mii_charinfo_t ci;
    mii_charinfo_init_default(&ci, 1);
    mii3d_view_t view;
    mii3d_view_init_default(&view);
    view.background = 0xFFEBEBEBu;
    mii3d_result_t r = MII3D_OK;
    if (!scene(t2, sr2, 0xEBEBEB, [&] {
            r = mii3d_render_head_draws(renderer, &ci, &view, M, M, [](void* u, const mii3d_draw_t* d) {
                Impl* g = static_cast<Impl*>(u);
                if (g->ringUsed == 0) {
                    const float* v = d->vertices;
                    g->log("first draw: %d vertices, %d indices (first %u %u %u); vertex 0: pos %.2f %.2f %.2f %.2f uv %.2f %.2f hl %.2f",
                        d->vertex_count, d->index_count, d->indices[0], d->indices[1], d->indices[2], v[0], v[1], v[2],
                        v[3], v[4], v[5], v[6]);
                }
                g->draw(d);
            }, this);
        }) || r != MII3D_OK)
        return false;
    std::vector<uint8_t> cpu((size_t)M * M * 4);
    mii3d_render_head(renderer, &ci, &view, cpu.data(), M, M);
    long total = 0;
    int bad = 0;
    for (int y = 0; y < M; y++)
        for (int x = 0; x < M; x++) {
            const uint8_t* g = pixel(t2, x, y);
            const uint8_t* c = &cpu[((size_t)y * M + x) * 4];
            int d = 0;
            for (int k = 0; k < 3; k++)
                d = std::max(d, std::abs((int)g[k] - (int)c[k]));
            total += d;
            bad += d > 48;
        }
    const float mean = (float)total / (M * M);
    const uint8_t* gc = pixel(t2, M / 2, M / 2);
    const uint8_t* cc = &cpu[((size_t)(M / 2) * M + M / 2) * 4];
    log("test 3 (Mii vs CPU): mean difference %.1f, %d of %d pixels far off; center GPU (%d,%d,%d) CPU (%d,%d,%d)",
        mean, bad, M * M, gc[0], gc[1], gc[2], cc[0], cc[1], cc[2]);
    // save both for comparison
    for (int which = 0; which < 2; which++) {
        FILE* f = std::fopen(which ? "ux0:data/mii-maker-vita/gpu_test_cpu.ppm" : "ux0:data/mii-maker-vita/gpu_test_gpu.ppm", "wb");
        if (!f)
            continue;
        std::fprintf(f, "P6\n%d %d\n255\n", M, M);
        for (int y = 0; y < M; y++)
            for (int x = 0; x < M; x++)
                std::fwrite(which ? &cpu[((size_t)y * M + x) * 4] : pixel(t2, x, y), 1, 3, f);
        std::fclose(f);
    }
    release((const void*)this);
    release((const void*)&textures);
    // MSAA edges and gamma rounding differ a little; garbage differs a lot
    return mean < 10.0f && bad < M * M / 20;
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
    NVGXMwindow* window = gxmGetWindow();
    if (!window || !window->context) {
        why = "no GXM context";
        log("%s", why.c_str());
        return false;
    }
    impl->ctx = window->context;
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

    // Build the shaders and check the results against the CPU renderer;
    // try a couple of setups (vertex padding, compiler mode) before giving
    // up, and log each attempt.
    const struct {
        int stride;
        bool fx;
    } setups[] = { { kFloats * 4, true }, { 64, true }, { kFloats * 4, false }, { 64, false } };
    for (const auto& setup : setups) {
        impl->stride = setup.stride;
        impl->useFx = setup.fx;
        log("trying: %d-byte vertices, useFx %d", setup.stride, setup.fx ? 1 : 0);
        impl->vertGxp.clear();
        impl->fragGxp.clear();
        if (!impl->compile(kVertexShader, SCE_SHACCCG_PROFILE_VP, &impl->vertGxp) ||
            !impl->compile(kFragmentShader, SCE_SHACCCG_PROFILE_FP, &impl->fragGxp)) {
            log("shaders did not compile");
            continue;
        }
        log("shaders compiled: vertex %u bytes, fragment %u bytes", (unsigned)impl->vertGxp.size(),
            (unsigned)impl->fragGxp.size());
        if (!impl->createPrograms()) {
            impl->destroyPrograms();
            continue;
        }
        if (impl->selfTest()) {
            ok = true;
            why = "GPU";
            log("self-test passed: GPU rendering on");
            return true;
        }
        impl->destroyPrograms();
    }
    why = "the GPU self-test failed (details in gpu.log)";
    log("%s", why.c_str());
    return false;
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

    mii3d_result_t r = MII3D_OK;
    const bool drawn = g.scene(t, sr, background,
        [&] { r = mii3d_render_head_draws(g.renderer, &mii, &view, size, size, drawCallback, &g); });
    if (!drawn || r != MII3D_OK) {
        g.log("render: %s%s", mii3d_result_string(r), drawn ? "" : " (scene failed or vertex buffer full)");
        return -1;
    }
    return t->nvgImage;
}

void GpuRenderer::release(const void* owner)
{
    if (impl)
        impl->release(owner);
}

#endif
