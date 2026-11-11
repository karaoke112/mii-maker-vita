# 3D Mii heads

Mii Maker Vita draws heads in 3D from the **Wii U face resource**, a file
Nintendo ships on every Wii U. It is copyrighted, so it is **not** included in
this repository or the `.vpk`: you supply your own copy. Without it,
everything still works and previews use the built-in 2D renderer.

## Installing the resource

1. Dump it from your own Wii U (for example with an FTP homebrew such as
   ftpiiu): `/storage_mlc/sys/title/0005001b/10056000/content/FFLResHigh.dat`
   (`FFLResMiddle.dat` from the same folder also works, with lower-resolution
   textures).
2. Copy it to the Vita at **`ux0:data/mii/FFLResHigh.dat`** (VitaShell over
   FTP or USB). On the desktop build the folder is `./mii-data/`.
3. Start the app. The line under the "Create" buttons says
   "3D heads: FFLResHigh.dat" when it was found, or tells you where to put it.

`FFLResHighLG.dat` / `FFLResMiddleLG.dat` are not needed. Miitomo's
`AFLResHigh_2_3.dat` is a different variant and is not supported yet.

## How it works

`libmii3d/` (C99, depends on libmii and zlib):

1. **Resource reader** (`src/res.c`): the big-endian `FFRA` container (header,
   317 texture and 857 shape part tables), zlib-compressed parts, texture
   footers and shape headers.
2. **Texture untiling** (`src/gx2_untile.c`): Wii U textures are stored in
   the GPU's tiled layout (2D macro tiling, 1D for the tiny placeholders).
   This was written from the documented R6xx/R7xx tiling rules; it matched
   GX2's own address library byte-for-byte on all 796 textures in the four
   resource files, and the tests pin it to addrlib-derived checksums.
3. **Software rasterizer** (`src/raster.c`): perspective-correct, bilinear
   textures, depth buffer, alpha blending, and the color modes Mii parts
   use. Doing this on the CPU avoids shader-compiler requirements on the
   Vita and gives identical output on Vita, desktop and in tests.
4. **Character model** (`src/charmodel.c`): composes the face texture (eyes,
   brows, mouth, mustache, mole) and the faceline texture (skin, wrinkles,
   makeup, texture beards), then draws faceline, forehead, hair, mask, nose,
   nose line, glasses and beard shapes with simple lighting.

The app's `HeadRenderer` runs this on a worker thread: a quick pass shows up
first, then a 2x-supersampled pass replaces it, and results are cached so
library thumbnails are only rendered once.

Speed (desktop CPU, 256 px): about 6 ms quick pass, 21 ms smooth pass. Expect
very roughly 10-20x that on a Vita, i.e. a quick pass in the low hundreds of
milliseconds. This has not been measured on hardware yet.

## Tools

```sh
cmake -S libmii3d -B build/mii3d && cmake --build build/mii3d
build/mii3d/miirender FFLResHigh.dat head.ppm some.charinfo 256 20   # yaw 20
build/mii3d/miirender FFLResHigh.dat --face face.ppm some.ffsd 256   # face texture
MII3D_RES=FFLResHigh.dat build/mii3d/test_mii3d                       # also test a real file
```

## Known gaps

- Head only (the body comes from a separate resource, `FFLBodyRes.dat`); the
  app draws a simple shirt in the favorite color under it.
- No hats, no expressions other than the default, no Switch-only parts
  that the Wii U resource lacks (Switch glasses 9-19 are shown as their
  closest Wii U style, as libmii does when converting).
- Part grids in the editor still show numbers; rendering part thumbnails is
  the next step.
