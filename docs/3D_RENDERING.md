# 3D Mii heads

Mii Maker Vita draws heads in 3D from a **face resource file** that you copy
from hardware or software you own. These files are Nintendo's copyrighted
data, so they are **not** included in this repository or the `.vpk`. Without
one the app still works (create, edit, save, export), but previews only say
where to put the file.

## Installing a resource

Copy one of these to **`ux0:data/mii/`** on the Vita (on the desktop build:
`./mii-data/`). The first one found is used:

| File | From | Notes |
| --- | --- | --- |
| `AFLResHigh_2_3.dat` | Miitomo (Android/iOS app data) | Preferred: has all 20 Switch glasses styles and higher-resolution textures |
| `FFLResHigh.dat` | Wii U: `/storage_mlc/sys/title/0005001b/10056000/content/` (e.g. via ftpiiu) | Switch-only glasses styles 9-19 are shown as their closest Wii U style |
| `FFLResMiddle.dat` | same folder | Lower-resolution textures |

The `LG` variants are not needed. The line under the "Create" buttons shows
which file is in use.

## How it works

`libmii3d/` (C99, depends on libmii and zlib):

1. **Resource reader** (`src/res.c`): the big-endian `FFRA` container. Wii U
   and Miitomo files differ in their texture tables (Miitomo 2_3 has 80
   eyes, 28 eyebrows, 52 mouths, 20 glasses), so the layout is detected by
   checking that typical parts decode.
2. **Texture untiling** (`src/gx2_untile.c`): Wii U textures are stored in
   the GPU's tiled layout (2D macro tiling, 1D for the tiny placeholders);
   Miitomo's are linear. The untiler was written from the documented
   R6xx/R7xx tiling rules; it matched GX2's own address library
   byte-for-byte on all 796 textures in the four Wii U files, and the tests
   pin it to addrlib-derived checksums.
3. **Software rasterizer** (`src/raster.c`): perspective-correct, bilinear
   textures, depth buffer, alpha blending, the color modes Mii parts use,
   and the **Switch Mii lighting model** (half-Lambert diffuse, subsurface
   tint, specular with the hair's per-vertex blend, rim light, in linear
   color) with nn::mii's per-part material tables. Doing this on the CPU
   avoids shader-compiler requirements on the Vita and gives identical output
   on Vita, desktop and in tests.
4. **Character model** (`src/charmodel.c`): composes the face texture (eyes,
   brows, mouth, mustache, mole) and the faceline texture (wrinkles, makeup,
   texture beards), then draws faceline, forehead, hair, mask, nose, nose
   line, glasses and beard shapes.

Editor grid icons come from the same renderer: flat parts (eyes, brows,
mouth, mustache, glasses) via `mii3d_render_part_icon`, shapes (hair, face
shape, wrinkles, makeup, nose, beard) as small heads framed on the part,
all in the Mii's current colors and only for tiles on screen.

The app's `HeadRenderer` runs this on a worker thread: a quick pass shows up
first, then a 2x-supersampled pass replaces it, and results are cached so
library thumbnails are only rendered once.

Speed (desktop CPU, 256 px): about 5 ms quick pass, 18 ms smooth pass. Expect
very roughly 10-20x that on a Vita. This has not been measured on hardware yet.

## Tools

```sh
cmake -S libmii3d -B build/mii3d && cmake --build build/mii3d
build/mii3d/miirender FFLResHigh.dat head.ppm some.charinfo 256 20   # yaw 20
build/mii3d/miirender FFLResHigh.dat --face face.ppm some.ffsd 256   # face texture
MII3D_RES=FFLResHigh.dat build/mii3d/test_mii3d                       # also test a real file
```

## Known gaps

- Head only for now. The body lives in `FFLBodyRes.dat` (body shapes and
  skeleton); support is next, and the Switch body/pants materials are
  already in `materials.inc`.
- No hats and no expressions other than the default.
- Miitomo's older `AFLResHigh.dat` (9 glasses) is recognised by layout but
  has not been tested with a real file.
- Part icons are rendered at runtime (and cached in memory), not saved to
  disk yet, so the first visit to a tab fills its grid progressively.
