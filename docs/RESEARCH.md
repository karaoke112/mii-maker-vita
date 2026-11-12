# Research notes

What exists today (November 2026) for building a Switch-style Mii editor on
the PS Vita, what each option is licensed under, and what this repo uses.
Items marked **verified** were cloned, built or run while writing this.

## 1. UI toolkit: what Vita homebrew uses

| Library | Platforms | Vita? | License | Notes |
| --- | --- | --- | --- | --- |
| [borealis (xfangfang fork)](https://github.com/xfangfang/borealis) | Switch, PC, Android, iOS, PS4, **PS Vita** | Yes: GXM (NanoVG-GXM) or OpenGL ES 2 (PVR_PSP2 + SDL2) | Apache-2.0 | A Switch-system-UI-style toolkit: `AppletFrame`, `TabFrame` + `Sidebar`, focus navigation, button hints, IME. Ships on Vita in [wiliwili](https://github.com/xfangfang/wiliwili). **Verified**: its demo and this app build to `.vpk` with VitaSDK 2026.08 (GXM backend). |
| [Plutonium](https://github.com/XorTroll/Plutonium) | Switch only | No | MIT | Built on libnx + SDL2; porting means replacing its libnx parts. Not worth it when borealis already runs on Vita. |
| "libswitchgui" | n/a | n/a | n/a | No library by that name turned up; likely a mix-up with borealis or Plutonium. |
| [vita2d](https://github.com/xerpi/libvita2d) | Vita | Yes | not checked | 2D drawing lib used by many classic tools (e.g. VitaShell). You build every widget yourself. |
| [vitaGL](https://github.com/Rinnegatamante/vitaGL) + Dear ImGui | Vita | Yes | not checked / MIT | OpenGL-on-GXM; used by VitaDB Downloader, Vita Homebrew Sorter and many game ports. ImGui looks like a debug tool, not like the Switch. |
| [SDL2 for Vita](https://wiki.libsdl.org/SDL2/README-vita) | Vita | Yes | zlib | Windowing/input layer under borealis-GLES2 and most ports. |

**Choice: borealis**, because it already *is* a Switch-style UI on the Vita.
Two caveats found while building:

- `third_party/borealis/resources/font/` contains Nintendo's system fonts
  (`switch_font.ttf` is Morisawa UD Shin Go licensed to Nintendo,
  `switch_icons.ttf` is "NintendoExt", © Nintendo). This app ships its own
  `resources/` with M PLUS Rounded 1c (OFL) and PlayStation button glyphs
  from PromptFont (OFL) instead, and never packages borealis' `resources/`.
- On desktop, borealis' global quit binds START on every screen, which would
  steal the editor's START = save; the app binds it per screen instead.

## 2. Official Mii formats

All sizes in bytes. "Switch" formats are what a Switch-style editor should
store natively.

| Format | Size | Used by | Notes |
| --- | --- | --- | --- |
| CharInfo (`.charinfo`) | 0x58 | Switch games, `nn::mii` | One byte per field. The editing model. |
| CoreData (`.nfcd`) | 0x30 | Switch | Bit-packed CharInfo without the create ID. |
| StoreData (`.nfsd`) | 0x44 | Switch NAND database | CoreData + 16-byte create ID + data CRC16 + device CRC16 (seeded with the console's Mii author ID). |
| NFDB (`.nfdb`) | 0x1A98 | Switch NAND (`MiiDatabase.dat`) | `"NFDB"` + 100 × StoreData + version + count + CRC16. |
| Ver3StoreData (`.ffsd`/`.cfsd`) | 0x60 | 3DS, Wii U, amiibo, Mii QR codes (after decryption) | 6–8 colors per part instead of 100; 9 glasses instead of 20; adds birthday and creator name. |
| RFL (`.rcd`/`.rsd`) | 0x4A / 0x4C | Wii (Mario Kart Wii, Super Mario Galaxy, …) | Fewer parts again (e.g. 72 hairstyles); separate format, not implemented yet. |

Sources for the layouts:

- [MiiJS](https://github.com/Stewared/MiiJS) (ISC). `Stewared/MiiJS` and
  `KestronProgramming/MiiJS` are the same repository (same commit
  `de10c3c`). It reads/writes ~40 formats and renders, but it is
  Node/browser JavaScript with native dependencies (`canvas`, `sharp`,
  `three`, `webgpu`), so it cannot run on a Vita. **Used here as the
  reference implementation**: libmii's encoders are checked byte-for-byte
  against it (see §5), and the Switch→Ver3 palette tables and the Switch
  editor's on-screen grid order come from its data tables.
- Ryujinx's `nn::mii` service (MIT): the CoreData bit table, StoreData/NFDB
  CRC rules and the base "default" Miis.
- [libnx `mii.h`](https://switchbrew.github.io/libnx/mii_8h.html) (ISC):
  CharInfo field order.
- [3dbrew](https://www.3dbrew.org/wiki/Mii) / [Wiibrew](https://wiibrew.org/wiki/Mii_data):
  Ver3 and Wii layouts. [HEYimHeroic/MiiDataFiles](https://github.com/HEYimHeroic/MiiDataFiles):
  hundreds of real sample files plus format notes.

## 3. Existing Mii editors and renderers

| Project | What it is | License | Usable here? |
| --- | --- | --- | --- |
| [aboood40091/ffl](https://github.com/aboood40091/ffl) | Decompilation of the Wii U Face Library (FFL v1.3.10), the code that builds and draws Mii heads | **No license file** | Reference only: without a license it cannot be copied into this project. |
| [ariankordi/ffl](https://github.com/ariankordi/ffl) + [FFL-Testing](https://github.com/ariankordi/FFL-Testing) | FFL ported to PC (OpenGL 3.3 / ES 3.0 via RIO), plus a Switch-style shader and `nn::mii` color tables | Arian Kordi's changes: Unlicense. Base decomp: unlicensed | His additions (e.g. the 100-color palette used in `libmii/src/mii_editor.c`) can be used; the decompiled core cannot. |
| [FFL.js](https://github.com/ariankordi/FFL.js) | FFL renderer for browsers | AGPL-3.0 | Not for a Vita app. |
| [vfl / vee](https://tangled.org/j0.lol/vee) | Rust Mii parser + renderer reimplemented from the decompilations | MIT OR Apache-2.0 | Best permissive reference for the resource format when writing our renderer. |
| Arian Kordi's "Fusion" ([post](https://ariankordi.net/posts/2026-mii-fusion/)) | One Mii library transpiled to C, C++, JS, … | Planned permissive | Not released yet (data part estimated for late 2026, renderer later). Worth adopting when it lands. |
| [MiiKit](https://github.com/cmyksoda/MiiKit) | C library for Wii homebrew: Wii Mii data, `RFL_DB.dat`, draws heads from `RFL_Res.dat` with GX | CC0 | Yes, for the Wii-format work (§6). `-DMII_NO_OGC` builds its data code on a PC. |
| [ogws](https://github.com/doldecomp/ogws) | Wii Sports decompilation, includes `src/RVLFaceLib` (the Wii's RFL) | CC0 | Yes, reference for an RFL shim in Wii game ports. |
| [datkat21/mii-creator](https://github.com/datkat21/mii-creator), [kariyamii/MiiCreator](https://github.com/kariyamii/MiiCreator) | Web Mii editors | no license file / not checked | Web apps; nothing to port to a Vita. |
| [MiiPort](https://github.com/Genwald/MiiPort) | Switch homebrew to import/export Miis | ISC-style | Useful companion: moves our `.charinfo`/`.nfsd` exports onto a real Switch. |
| [mii-maker-ds](https://github.com/abudndle/mii-maker-ds), [Miichanic](https://github.com/FoofooTheGuy/Miichanic_Plugin) | DS demake (unfinished), 3DS plugin | — | Not applicable. |

**Conclusion:** no existing Switch-style editor can simply be forked onto the
Vita, and no permissively licensed C/C++ library covered the Switch formats.
So this repo has its own small C library (`libmii`) for the data and uses
borealis for the UI. The 3D renderer still has to be written (plan in
[ARCHITECTURE.md](ARCHITECTURE.md#renderer)).

### Rendering needs Nintendo's resource files

Every faithful renderer draws from Nintendo's own part models and textures.
They are copyrighted, so they cannot be shipped in this repository or the
`.vpk`; users dump them from hardware they own:

- `FFLResHigh.dat` — Wii U (`sys/title/0005001b/10056000/content/`)
- `AFLResHigh_2_3.dat` — Miitomo (same format as FFLResHigh)
- Switch `ShapeHigh.dat` and its texture archive — the Switch's Mii system archive
- `RFL_Res.dat` — Wii NAND (MiiKit can read it)

## 4. Ad-hoc trading between Vitas

VitaSDK exposes the Vita's native ad-hoc stack:
[`SceNetAdhocMatching`](https://docs.vitasdk.org/group__SceNetAdhocMatchingUser.html)
(peer discovery + small messages), plus
[`pspnet_adhoc`](https://docs.vitasdk.org/pspnet__adhoc_8h.html) /
[`pspnet_adhocctl`](https://docs.vitasdk.org/pspnet__adhocctl_8h_source.html)
(PDP/PTP sockets). A Mii is 68 bytes as StoreData, so a whole 100-Mii database
fits in a few packets. Design in [ARCHITECTURE.md](ARCHITECTURE.md#ad-hoc-trading).

## 5. Verification done for libmii

- Against MiiJS, on 368 real files from MiiDataFiles (498 conversions): CharInfo→CoreData,
  →StoreData (incl. data CRC), →Ver3; CoreData→CharInfo; Ver3→CharInfo.
  All byte-identical (after MiiJS-style zeroing of junk after a name's NUL).
- Ver3 → CharInfo → Ver3 round-trips all 203 real 3DS/Wii U samples
  byte-for-byte, CRC included.
- 6,189 unit-test checks (ASan + UBSan clean), including 24 synthetic Miis
  whose MiiJS-encoded bytes are committed as test vectors.
- Typos found in MiiJS' Switch color-grid table (59, 62, 75 duplicated;
  58, 61, 76 missing) are corrected in libmii and checked by a test.

## 6. Games using players' Miis

- **Mario Kart Wii:** [Wiicompiled](https://recomp.gg/g/mario-kart-wii) is a
  fan static recompilation for PC (v0.9.3, Oct 2026, graphics via Aurora); the
  matching decompilation is early. Players supply their own game files.
- **Super Mario Galaxy:** [Petari](https://github.com/smgcommunity/petari)
  reached 100% code decompilation in Sept 2026; an unofficial PC port
  ("AstroCore") has been announced, but its status is unconfirmed.
- Both are Wii games: they read Miis through RFL in the Wii format (§2). A
  Vita port would need a conversion from the shared Switch-format database to
  Wii format, plus an RFL shim (see [ARCHITECTURE.md](ARCHITECTURE.md#games)).

## 7. Links reviewed (November 2026)

| Link | What it is | License | Use here |
| --- | --- | --- | --- |
| [mii-unsecure.ariankordi.net](https://mii-unsecure.ariankordi.net/) | Web front end for Arian Kordi's FFL-based render server | — | Reference for how renders should look |
| [FFL-Testing `renderer-server-prototype`](https://github.com/ariankordi/FFL-Testing/tree/renderer-server-prototype) | PC renderer built on the FFL decompilation, with Switch- and Miitomo-style shaders | Arian Kordi's changes: Unlicense; FFL base: none | **Used**: the Switch shading model and nn::mii material tables (his own work, Unlicense) are ported into libmii3d. Its bundled body models are extracted game files and are not used |
| [aboood40091/ffl `nsmbu-win-port`](https://github.com/aboood40091/ffl/tree/nsmbu-win-port) | The FFL decompilation ported to PC | No license | Reference only; libmii3d is an independent implementation |
| [MiiPort `convert_mii.h`](https://github.com/Genwald/MiiPort/blob/4ee38bbb8aa68a2365e9c48d59d7709f760f9b5d/include/convert_mii.h#L8) | Ver3 / CharInfo / CoreData conversion for Switch homebrew | ISC-style | Cross-check for libmii; also the model for asking users for the QR key |
| [kazuki-4ys MiiInfoEditorCTR](https://github.com/kazuki-4ys/kazuki-4ys.github.io/blob/148dc339974f8b7515bfdc1395ec1fc9becb68ab/web_apps/MiiInfoEditorCTR/mii.js#L103) | Web 3DS Mii editor; the linked code encodes Mii Studio data | No license | Reference for the Studio format |
| [jaames Mii QR gist](https://gist.github.com/jaames/96ce8daa11b61b758b6b0227b55f9f78) | Decrypts Mii QR payloads (AES-CCM) | — | Reference for QR support |
| [maboii.js](https://github.com/Entrivax/maboii.js) | amiibo encryption in JS | MIT | Reference for amiibo support |
| [nimiq/qr-scanner](https://github.com/nimiq/qr-scanner), [qr.js](https://github.com/educastellano/qr.js) | Browser QR scanning / generation | MIT | References; the Vita build will use quirc (ISC) and Nayuki's generator (MIT) in C |

None of them is a renderer that could be dropped into a Vita app as is:
every faithful renderer builds on the unlicensed FFL decompilation and
desktop OpenGL, or is JavaScript. The usable pieces were the published
shading model and the format knowledge, which libmii3d now builds on.
