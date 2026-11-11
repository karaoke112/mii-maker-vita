# Architecture and roadmap

```
┌────────────────────────────── app/ (C++17, borealis) ──────────────────────────────┐
│  LibraryActivity ── EditorActivity ── (planned) TradeActivity, ImportActivity       │
│        │                 │  OptionGrid / FieldSlider / MiiPreview                   │
│        ▼                 ▼                                                          │
│  MiiLibrary: ux0:data/mii/database.nfdb  ◄── shared with other apps & game ports   │
└────────┬───────────────────────────────────────────────────────────────────────────┘
         │ links
┌────────▼──────── libmii/ (C99, no I/O, no malloc) ───────────────────────────────────┐
│ mii.h         CharInfo · CoreData · StoreData · NFDB · Ver3StoreData · CRC16         │
│ mii_editor.h  Switch palette (100 colors, 10 skins) · Switch editor grid orders      │
│ (planned)     Wii RFL format · random Mii generation · QR (Ver3 + AES-CCM)           │
└──────────────────────────────────────────────────────────────────────────────────────┘
┌──────── libmii3d/ (C99 + zlib) ─────────────────────────────────────────────────────┐
│ FFRA resource reader · GX2 untiling · software rasterizer · head model              │
│ input: the user's own FFLResHigh.dat   output: an RGBA image                        │
└──────────────────────────────────────────────────────────────────────────────────────┘
         ▲ also linked by
   game ports / other homebrew (read the shared database, convert to what they need)
```

## Principles

- **Official formats only on disk.** The database is a byte-exact Switch
  NFDB; exports are `.charinfo`, `.nfsd`, `.ffsd`. Nothing invented.
- **libmii stays tiny and portable** (C99, no allocation, no file I/O) so it
  can be dropped into a Vita app, a decompiled game, a PC tool or a Switch
  homebrew without dragging dependencies along.
- **No Nintendo assets in the repo or the `.vpk`.** Fonts, icons and LiveArea
  art are original or openly licensed. The 3D renderer will load
  resource files the user dumps from their own hardware.
- **Match the Switch editor's information layout** (categories, grid order,
  100-color palette, adjustment ranges) so Miis made on a Switch can be
  rebuilt by muscle memory, while keeping our own visual design.

## Data model

`mii_charinfo_t` is the in-memory model everywhere: one byte per field, the
same layout as the Switch's CharInfo. The editor manipulates fields through
`mii_field_get/set` (which clamp to the official ranges), so every slider and
grid is generic.

Conversion matrix:

| from \ to | CharInfo | CoreData | StoreData | Ver3 | Wii |
| --- | --- | --- | --- | --- | --- |
| CharInfo | — | lossless | lossless | lossy (colors, glasses) | planned, lossy (parts) |
| Ver3 | lossless (+ `mii_ver3_info_t` for birthday etc.) | lossless | lossless | — | planned |

## Renderer

Implemented in `libmii3d/`; details in [3D_RENDERING.md](3D_RENDERING.md).
It draws from the Wii U face resource (`FFLResHigh.dat`), which users supply
from their own console. Without it, `MiiPreview` falls back to a 2D NanoVG
drawing that reacts to every field.

Rendering happens on the CPU (a software rasterizer) rather than through GXM
shaders. The Vita has no free shader compiler: `.gxp` files need Sony's
tools, or a runtime compiler extracted from the console. A head is a few
thousand triangles, and the editor only re-renders on change, on a worker
thread, quick pass first and smooth pass after. The result is uploaded as an
ordinary NanoVG image, so it works with any borealis backend. A GXM backend
could be added later behind the same API if profiling on hardware calls for it.

Next steps: part thumbnails for the editor grids (render each part once
into `ux0:data/mii/cache/`), expressions, the body resource, and Miitomo's
`AFLResHigh_2_3.dat` (linear textures, slightly different header).

## Ad-hoc trading

Goal: two Vitas in the same room swap Miis with no access point.

- **Discovery:** `sceNetAdhocMatching` in P2P mode with a fixed app-specific
  port; the "hello" payload carries the player's nickname and how many Miis
  they offer.
- **Transfer:** each Mii is sent as its 68-byte StoreData; a message is
  `{ magic 'MIIX', version, count, StoreData[count] }`, with a CRC16 over the
  whole message. Receivers re-check every StoreData's data CRC, then re-sign
  the device CRC with their own author ID when saving.
- **Conflicts:** same create ID ⇒ "update this Mii?" prompt; database full
  (100) ⇒ refuse with a message.
- **Code layout:** `libmii-net/` (C) with a transport interface, so the same
  protocol can later run over UDP on a normal Wi-Fi network (and be tested on
  a PC). The UI is a `TradeActivity` listing nearby players.

## Games

The shared database is the infrastructure: a game port links libmii, opens
`ux0:data/mii/database.nfdb` read-only, and converts each Mii to the format the
game expects. Contract: [SHARED_DATABASE.md](SHARED_DATABASE.md).

For Wii games (Mario Kart Wii, Super Mario Galaxy):

1. **Wii format in libmii** (`mii_rfl_encode/decode`, 0x4A RCD / 0x4C RSD).
   Switch → Wii is lossy (more parts and colors than the Wii has); MiiJS has
   backport tables for parts and MiiKit (CC0) implements the Wii format.
2. **RFL shim**: Wii games call RVLFaceLib (`RFLGetStoreData`,
   `RFLGetMiddleDB…`). A port replaces RFL's NAND database access with reads
   from the shared NFDB via libmii. The CC0 RFL decompilation in
   [ogws](https://github.com/doldecomp/ogws) shows exactly which functions to
   cover.
3. **Rendering in the game**: RFL draws heads from `RFL_Res.dat`; ports
   already need the player's own game/system files, and MiiKit shows how to
   load them.

For Switch-era or new homebrew: read CharInfo directly; no conversion.

## Roadmap

| Phase | Scope | Status |
| --- | --- | --- |
| 0 | Research, libmii (Switch + Ver3 + NFDB), tests against MiiJS | done |
| 1 | Editor app on borealis: library, all categories, save/export, Vita `.vpk` | done |
| 2 | 3D renderer from the user's own Wii U resource, with 2D fallback | done (heads); part thumbnails next |
| 3 | Import: `.charinfo`/`.ffsd`/`.nfsd` files from `ux0:data/mii/import/`; Mii QR codes via the Vita camera (Ver3 AES-CCM; key supplied by the user) | planned |
| 4 | Ad-hoc trading (`libmii-net`, `TradeActivity`) | planned |
| 5 | Wii format + RFL shim helpers for game ports | planned |
| 6 | Polish: random Mii ("look-alike") generator from nn::mii's tables, more languages, touch gestures, sounds | planned |
