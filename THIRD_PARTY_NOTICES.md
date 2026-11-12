# Third-party notices

## Code and data in libmii

### MiiJS (ISC)

<https://github.com/Stewared/MiiJS>. Used for the Switch → Ver3 palette and
glasses tables (`libmii/src/mii_ver3_tables.inc`), the Switch editor grid
orders (`libmii/src/mii_editor.c`, three duplicated color-grid entries
corrected), and to generate the reference test vectors in
`libmii/tests/vectors/`. MiiJS declares the ISC license in its
`package.json`:

> Copyright (c) Stewared and the MiiJS contributors
>
> Permission to use, copy, modify, and/or distribute this software for any
> purpose with or without fee is hereby granted, provided that the above
> copyright notice and this permission notice appear in all copies.
>
> THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
> WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
> MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
> SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
> WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
> ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR
> IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

### Ryujinx (MIT)

The CoreData bit layout, StoreData/NFDB checksum rules and the two base
"default" Mii templates follow Ryujinx's `nn::mii` implementation.

> MIT License
>
> Copyright (c) Ryujinx Team and Contributors
>
> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to
> deal in the Software without restriction, including without limitation the
> rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
> sell copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in
> all copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
> FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
> DEALINGS IN THE SOFTWARE.

### Arian Kordi's FFL fork (Unlicense)

The sRGB values of the Switch common-color, faceline-color and upper-lip
tables in `libmii/src/mii_editor.c` come from `src/FFLiColor.cpp` in
<https://github.com/ariankordi/ffl>, which Arian Kordi released under the
Unlicense (public domain).

## Code and data in libmii3d

### vee / vfl (MIT OR Apache-2.0)

<https://github.com/j0lol/vee>, © Jo Null. The face-part layout constants and
formulas (eye/eyebrow/mouth/mustache/mole positions, scales, rotation offset
tables), the part placement offsets (nose, glasses, hair, beard) and the
color modulation modes in `libmii3d/src/charmodel.c` and `raster.c` follow
vee's MIT-licensed implementation. Used under the MIT license:

> Copyright (c) Jo Null
>
> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to
> deal in the Software without restriction, including without limitation the
> rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
> sell copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in
> all copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
> FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
> DEALINGS IN THE SOFTWARE.

### Arian Kordi's FFL-Testing (Unlicense)

The Switch lighting model in `libmii3d/src/raster.c` / `charmodel.c` and the
material tables in `libmii3d/src/materials.inc` (generated) come from
`src/ShaderSwitch.cpp` and `fs/content/shaders/SampleShader.frag` in
<https://github.com/ariankordi/FFL-Testing>, written by Arian Kordi and
released under the Unlicense (public domain).

### zlib

libmii3d links zlib (zlib license, © Jean-loup Gailly and Mark Adler) to
inflate resource parts.

### Not included: Nintendo's face resource

`FFLResHigh.dat` and friends are Nintendo's copyrighted data. They are not in
this repository or the `.vpk`; users provide their own (`*.dat` is in
`.gitignore`). The FFRA layout was learned from the FFL decompilation's
public headers (reference only, no code copied). The GX2 untiler was written
from the documented tiling rules and only *checked* against ninTexUtils
(GPL-3.0) outside this repository; no ninTexUtils code is included.

## Bundled with the app

- **borealis** (`third_party/borealis`, git submodule): Apache License 2.0,
  © borealis contributors. `app/resources/i18n/en-US/hints.json` and
  `app/resources/img/sys/cursor.png` are copied from it. borealis'
  own `resources/font/` (Nintendo system fonts) is **not** used or shipped.
- **Material Icons** (`app/resources/material/`): Apache License 2.0,
  © Google; license text alongside.
- **Poppins** (`app/resources/font/font.ttf`, the UI font): SIL Open Font
  License 1.1, © 2020 The Poppins Project Authors
  (<https://github.com/itfoundry/Poppins>); license text in
  `app/resources/font/Poppins-OFL.txt`.
- **M PLUS Rounded 1c** (`app/resources/font/mplus-rounded-1c.ttf`, the
  fallback for characters Poppins lacks, such as Japanese): SIL Open Font
  License 1.1, © 2016 The Rounded M+ Project Authors; license text in
  `app/resources/font/MPLUS-OFL.txt`.
- **Button glyph font** (`app/resources/font/icon.ttf`): a subset of
  **PromptFont** by Yukari Hafner (<https://shinmera.com/projects/promptfont>),
  SIL Open Font License 1.1, © 2018-2026 Yukari Hafner, © 2011-2016 Severin
  Meyer. `app/tools/make_button_font.py` copies its PlayStation glyphs to the
  code points borealis uses, enlarges the small ones and renames the font;
  license text in `app/resources/font/PromptFont-LICENSE.txt`.
- **Icons** (`app/resources/icons/`, plus the app icon and LiveArea images
  in `app/sce_sys/`): rasterized by `app/tools/make_icons.py` from
  Material Symbols (Google, Apache License 2.0), Material Design Icons
  (Pictogrammers, Apache License 2.0), Health Icons (Resolve to Save Lives,
  CC0 1.0) and Tabler Icons (Paweł Kuna, MIT). Every icon,
  its source and license: `app/resources/icons/LICENSES.md`.

No artwork in this repository was drawn or generated by this project:
icons and glyphs come from the sets above, and the only other images are
screenshots of the app.

## References only (no code copied)

FFL decompilation (aboood40091/ffl, no license), ninTexUtils (GPL-3.0,
used only to verify the untiler), libnx `mii.h`, 3dbrew,
Wiibrew, HEYimHeroic/MiiDataFiles, MiiKit, ogws.
