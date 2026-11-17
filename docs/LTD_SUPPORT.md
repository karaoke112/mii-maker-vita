# Tomodachi Life: Living the Dream support: what it would take

Branch `claude/tomodachi-ltd`. Goal: a **Living the Dream** tab of up to 70
Miis, to the left of Your Miis, and an option to draw every Mii with the
game's own look.

This page collects what is publicly known (November 2026) and a plan. It is
split into two parts of very different size: the **save data** (well
understood, doable) and the **renderer** (the hard part).

## 1. The save data

### Where it comes from

The game keeps its Miis in `Mii.sav` (residents) and `Player.sav` (the
player, the draft Mii, face paints) in its Switch save. A Vita cannot reach
a Switch, so the files travel by hand, as with Azahar's 3DS database:

- out of the Switch with a save manager (JKSV, on a modded Switch) or an
  emulator's save folder;
- into `ux0:data/mii-maker-vita/ltd/` (`Mii.sav`, `Player.sav`);
- back the same way after editing.

The app would treat them as it treats `CFL_DB.dat`: change only what it
must, keep `.bak` / `.orig` copies, refuse a file it cannot round-trip.

### The container format (documented)

From the community reference at [docs.ltdsave.app](https://docs.ltdsave.app/)
(container, hash and Mii pages):

- Little-endian. A fixed header, a flat entry table grouped by data type,
  then a heap.
- Header: `0x00` magic `0x01020304`; `0x04` format version (the game's
  `FormatVersion`); `0x08` offset of the heap; zero padding to `0x20`.
- Entry table from `0x20` to the heap: packed 8-byte `(hash, slot)`
  pairs; each type group starts with a sentinel (`hash == 0`,
  `slot == DataType`), in a fixed order from `Bool` to `Bool64bitKey`.
- The 4-byte slot holds small values; bigger ones (`Binary`, arrays,
  64-bit values, strings) live in the heap and the slot is their offset.
  Variable-length payloads start with a u32 length (bytes for `Binary`,
  elements for arrays); `BinaryArray` is a count of length-prefixed
  `Binary`s. Heap payloads are not aligned.
- Field names are never stored: each entry is the **Murmur3 x86 32-bit**
  hash (seed 0) of its UTF-8 name.
- No checksum, encryption or compression is mentioned (the open-source
  editors re-encode the file byte for byte).

That is a small, regular format: a reader and writer is a few hundred lines
of C in libmii, testable by round-tripping a real file.

### The Miis in it

- Appearance: `CharInfoEx` (hash `0x881CA27A`), a `BinaryArray` with one
  record per resident. Records are 156 bytes (`0x9C`) per the ShareMii code;
  the documented field pattern adds up to 152, so 4 bytes are still unknown.
- It is **not** a standard Nintendo Mii format (not CharInfo `0x58`,
  StoreData `0x44`, CoreData `0x30` or Ver3). It is a Mii-like layout with
  more parts: create id; name (11 UTF-16 chars); font region, gender,
  height, build; face flags (bangs side, two-color back hair and bangs, eye
  shadow, mirrored mouth and mustache, high mole); face line; **upper and
  lower wrinkles**; **two makeup layers**; hair as a u16 type with **two
  colors** and **separate front and back hair types**; eyes, **eyelashes,
  eyelids**, eyebrows, nose, mouth, beard, mustache, glasses, mole.
- Names (64 bytes UTF-16 per slot), pronunciation (128 bytes), the 18
  personality values and other per-resident data are separate tables in
  the same file, also keyed by hash.
- 70 residents at most (Nintendo's FAQ; the demo datamine agrees).
- Each resident also has relationships, clothes, belongings, room, trouble
  and childcare data (several hundred more fields). The reference documents
  their names and types only partly so far.

### What the app can do with it, in steps

1. **libmii: `Mii.sav` reader and writer**: the container, Murmur3 keys,
   `CharInfoEx` and the name table. Tested by round-tripping your save byte
   for byte.
2. **Living the Dream tab, read only**: 70 places, each resident shown as
   the nearest Mii our renderer can draw (LTD's extra parts approximated, as
   the Wii format is).
3. **Editing residents' looks and names**: writes only the 156-byte record
   and the name, leaving everything else alone, so relationships, rooms and
   items stay as they are.
4. **New residents and copies between tabs**: a new resident needs all of
   their other data set up the way the game expects (personality, voice,
   room, relationships...). That needs more of the format worked out and
   tested in the game. Until then, the safe way is to edit an existing
   resident, which is how the community editors work too.
5. **An LTD editor mode**: the extra parts (front/back hair, two hair
   colors, eyelashes, eyelids, makeup layers, wrinkles) as their own
   options. The part lists and what each number looks like come from the
   game's data or from careful comparison in the game, so this follows step 6.

## 2. The game's renderer ("draw all Miis the LTD way")

This is a much bigger job, and some of it can't be known until we look at
the game's files.

- **Nintendo's files, not included.** As with the Wii U face resource, the
  models, textures and settings would come from your own copy of the game
  (its romfs), put on the memory card by you. Nothing from the game can be
  committed here.
- **Unknown file layout.** It isn't publicly documented where and how the
  game keeps its Mii heads, hair and body. Switch games usually use BFRES
  (models) with BNTX textures, often zstd-compressed (`.zs`). The app already
  reads Wii U BFRES (the poses), but the Switch versions differ: newer BFRES
  versions, and Tegra block-linear textures in BC formats that would need
  decoding.
- **The look comes from shaders.** The game's style (lighting, outlines,
  skin) is in its shaders, which can't be run on the Vita. They would have
  to be studied and approximated in our own GXM and CPU renderers.
- **The extra parts need models.** Separate front/back hair, eyelashes and
  eyelids aren't in the Wii U/Miitomo resources we draw from now. Drawing
  them at all depends on reading the game's models.
- **Vita performance.** Switch-era models and textures are heavier. They
  would need cutting down (texture size, level of detail) to keep the list
  and editor smooth.

Realistic order: first find and read the files (needs your romfs, see
below), then draw one head in the game's look on desktop, then the Vita
GPU path, then the extra parts. Each step tells us how big the next one is.

## 3. Licenses

- The **format facts** (layouts, hashes, offsets) are what the app would
  implement, written fresh in libmii's own style.
- [alexislours/ltd-save-editor](https://github.com/alexislours/ltd-save-editor)
  is AGPL-3.0: useful to compare behaviour against, but its code is not
  copied here.
- [Azkun/ShareMii](https://github.com/Azkun/ShareMii) has no license: read
  for understanding only.
- [docs.ltdsave.app](https://docs.ltdsave.app/) states no license on the
  pages read. Its text is not copied; the facts it documents are used.

## 4. What is needed from you

1. A copy of your **`Mii.sav` and `Player.sav`**, to test against. Best of
   all: two copies, one taken before and one after a single small change in
   the game (renaming a resident, changing one hairstyle), so each field can
   be confirmed.
2. For the renderer, later: the **file list of the game's romfs** (just the
   names and sizes, e.g. the Mii/character folders) so the Mii models can be
   found. The files themselves stay on your side, like the `.dat` files.

## 5. Plan on this branch

| Step | What | Size | Needs |
| --- | --- | --- | --- |
| 1 | libmii `Mii.sav` reader/writer + tests | small | a `Mii.sav` |
| 2 | LTD tab (70 places, read only), left of Your Miis | small | step 1 |
| 3 | Edit residents' looks and names, safe writes | medium | step 2, testing in the game |
| 4 | New residents, copies to/from other tabs | large | more of the format, testing in the game |
| 5 | Read the game's Mii models and textures (Switch BFRES/BNTX) | large | your romfs file list, then the files on your side |
| 6 | "LTD look" renderer option (desktop, then Vita GPU) | large | step 5 |
| 7 | LTD editor mode with the extra parts | medium | steps 1 and 5 |

Sources: [docs.ltdsave.app](https://docs.ltdsave.app/),
[ltd-save-editor](https://github.com/alexislours/ltd-save-editor),
[ShareMii](https://github.com/Azkun/ShareMii),
[Nintendo Everything on the 70-Mii FAQ](https://nintendoeverything.com/tomodachi-life-living-the-dream-faq-details-mii-registration-limit-nintendo-switch-2-improvements-and-more/),
[Wikipedia](https://en.wikipedia.org/wiki/Tomodachi_Life:_Living_the_Dream),
[The Models Resource](https://models.spriters-resource.com/nintendo_switch/tomodachilifelivingthedream/).
