# 3D Miis

Mii Maker Vita draws Miis in 3D from a **face resource file** that you copy
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

The `LG` variants are not needed.

For **whole Miis** (body and pants, scaled by height and build), also copy
**`FFLBodyRes.dat`** to the same folder. It ships with Wii U titles built on
the Nintendo Web Framework (Arian Kordi's
[nwf-mii-cemu-toy](https://github.com/ariankordi/nwf-mii-cemu-toy) README
lists some). Without it the editor shows heads only.

The line under the "Create" buttons shows which files are in use.

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
5. **Body** (`src/charmodel.c`, `mii3d_res_open_body`): `FFLBodyRes.dat` is
   the same container with no textures and the two static bodies (male,
   female) in nose shape slots 3 and 4. The meshes are laid out for a
   texture atlas whose bottom band holds the legs and feet; those triangles
   (and the soles) are drawn as gray pants, the rest in the favorite color,
   with the Switch's body and pants materials. Height and build scale the
   body as nn::mii's `GetBodyScale` does, and the head sits on the neck at
   10.7766 x 8.715 x the height scale (formulas from Arian Kordi's
   FFL-Testing, public domain).

Editor grid icons come from the same renderer: flat parts (eyes, brows,
mouth, mustache, glasses) via `mii3d_render_part_icon`, shapes (hair, face
shape, wrinkles, makeup, nose, beard) as small heads framed on the part,
in neutral colors (black hair and lines, light skin; eyes and lips keep the
Mii's colors), and only for tiles on screen.

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
build/mii3d/miirender FFLResHigh.dat --body FFLBodyRes.dat mii.ppm female 400   # whole Mii
MII3D_RES=FFLResHigh.dat MII3D_BODY=FFLBodyRes.dat build/mii3d/test_mii3d   # also test real files
```

## Known gaps

- The body is the static Wii U one: no posing, and the pants are always the
  "own Mii" gray.
- No hats and no expressions other than the default.
- Miitomo's older `AFLResHigh.dat` (9 glasses) is recognised by layout but
  has not been tested with a real file.
- Part icons are rendered at runtime (and cached in memory), not saved to
  disk yet, so the first visit to a tab fills its grid progressively.
