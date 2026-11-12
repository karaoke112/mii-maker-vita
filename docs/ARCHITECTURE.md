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
│ mii_qr.c      Mii QR codes: AES-CCM, Switch 2 extension (keys from the user)         │
│ (planned)     Wii RFL format                                                         │
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
  art come from openly licensed, human-made sets (see
  THIRD_PARTY_NOTICES.md). The 3D renderer will load
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
It draws from a face resource users supply (`AFLResHigh_2_3.dat` from
Miitomo or `FFLResHigh.dat` from a Wii U). Without one, previews only explain
where to put it; there is deliberately no drawn stand-in.

Rendering happens on the CPU (a software rasterizer) rather than through GXM
shaders. The Vita has no free shader compiler: `.gxp` files need Sony's
tools, or a runtime compiler extracted from the console. A head is a few
thousand triangles, and the editor only re-renders on change, on a worker
thread, quick pass first and smooth pass after. The result is uploaded as an
ordinary NanoVG image, so it works with any borealis backend. On the Vita,
large previews can instead be drawn with GXM when the user has
`libshacccg.suprx` (see 3D_RENDERING.md): the CPU still prepares vertices and
materials, the GPU fills pixels.

Whole Miis come from the optional `FFLBodyRes.dat` (see 3D_RENDERING.md).
Next steps: expressions.

## QR codes

Goal: scan the Mii QR codes that 3DS, Wii U, Miitomo and Tomodachi Life
print, and create QR codes those consoles can scan, both on the Vita.

Done (phase 3b):

- **Payload** (`libmii/src/mii_qr.c`): 112 bytes for 3DS / Wii U: the Mii
  id's 8 bytes (also the AES-CCM nonce), then the rest of the Ver3StoreData
  encrypted (CCM over those 88 bytes plus 8 zeros, whose ciphertext is
  dropped), then the 16-byte tag. Switch 2 codes (0x90 bytes) add 0x11 and
  a second CCM-sealed block with the Switch colors and glasses Ver3 cannot
  hold (as in an amiibo's StoreData extension). Longer Tomodachi Life codes
  are read for their first 112 bytes. Our own AES-128 and CCM; checked
  against two real Switch 2 codes (MiiJS' test data).
- **Keys**: Nintendo's, not included. Like MiiPort, the app reads the
  user's copy from `ux0:data/mii-maker-vita/dat/qrkey.txt` (any layout of
  32 hex digits per key); libmii recognises each key by a check value.
- **Scanning** (`QrScanActivity`): the back camera (`sceCamera`, 640x480
  ABGR) on a worker thread into [quirc](https://github.com/dlbeer/quirc)
  (ISC); frames that find nothing are tried again with a local threshold
  (four variants, one per frame). Pictures of QR codes in the import folder
  go through the same scanner, which is how the desktop build is tested.
- **Creating** (`QrShowActivity`): [Nayuki's QR Code generator](https://github.com/nayuki/QR-Code-generator)
  (MIT) builds a Switch 2 code (3DS-format without the Switch 2 key),
  shown beside the Mii and saved as a PNG in `ux0:data/mii/qr/`.
- JavaScript libraries such as nimiq/qr-scanner and qr.js (MIT) do the same
  in browsers; they are useful references but cannot run on the Vita.
- Related later: Mii Studio codes (the hex strings Nintendo's website uses;
  see MiiJS and kazuki-4ys' MiiInfoEditorCTR for the obfuscation) and amiibo
  (maboii.js, MIT, shows the amiibo crypto; needs the user's own keys).

## Ad-hoc trading

Goal: two Vitas in the same room swap Miis with no access point.
First version done (phase 4), not yet tried on two Vitas:

- **Joining** (`trade::Link`, `app/src/trade.cpp`): the PSP-compatible
  ad-hoc stack the Vita offers (`ScePspnetAdhoc`): `sceNetAdhocInit`,
  `sceNetAdhocctlInit` with the app's id (`MIIV00001`), then the system's
  network dialog in `SCE_NETCHECK_DIALOG_MODE_PSP_ADHOC_CONN` mode, which
  borealis' frame loop already drives (`sceCommonDialogUpdate`). The same
  sequence is known to work on hardware in vitaQuake (studied, not copied:
  it is GPL).
- **Transport:** PDP datagrams on port 0x4D49, all sent to the broadcast
  address; desktop builds use UDP multicast on 127.0.0.1 instead, so two
  copies of the app trade with each other for testing.
- **Protocol** (`trade::Session`): messages `"MIIT"`, version, type, sender
  session id, recipient session id (0 = all), offer id, fields, CRC16.
  HELLO once a second (name, whether this console is receiving); OFFER
  repeated every 0.4 s until answered (sender name + the Mii's CharInfo, so
  it keeps its create id and replaces an older copy instead of making a
  second one); ACCEPT / DECLINE sent three times, and repeated if the offer
  comes again. A sender gives up after a minute.
- **Screens** (`TradeActivity`): Receive from Another Vita waits and asks
  before saving each Mii (with a preview); Send to Another Vita lists the
  receiving Vitas by user name.

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
| 2 | 3D renderer from the user's own resource (Wii U or Miitomo), Switch lighting | done (heads) |
| 2b | Rendered style icons in every part grid | done |
| 2c | Body from `FFLBodyRes.dat` (when provided) | done |
| 2d | Cache rendered icons on the memory card (`ux0:data/mii-maker-vita/cache/`) | done |
| 3 | Import `.charinfo`/`.ffsd`/`.nfsd` files from `ux0:data/mii/import/` | done |
| 3b | **QR codes**: scan Mii QR codes with the Vita camera and create them (see above) | done |
| 4 | Ad-hoc trading (`trade.cpp`, `TradeActivity`) | first version (needs testing on two Vitas) |
| 5 | Wii format + RFL shim helpers for game ports | planned |
| 6 | Polish: random Mii ("look-alike") generator from nn::mii's tables, more languages, touch gestures, sounds | planned |
| 6b | Dark mode (Settings: START in the library), saved in `ux0:data/mii-maker-vita/settings.txt` | done |
| 6c | Look-alikes: ten random Miis to start from (`mii_charinfo_init_random`) | done |
