# Building

Clone with submodules:

```sh
git clone --recursive https://github.com/karaoke112/mii-maker-vita
# or, in an existing clone:
git submodule update --init third_party/borealis
git -C third_party/borealis submodule update --init --depth 1 library/lib/extern/glfw   # desktop only
```

## libmii and its tests (any OS)

```sh
cmake -S libmii -B build/libmii
cmake --build build/libmii
ctest --test-dir build/libmii          # or: build/libmii/test_mii
build/libmii/miitool info some.charinfo
build/libmii/miitool convert in.ffsd out.charinfo
build/libmii/miitool db database.nfdb
```

Regenerate the MiiJS reference vectors (needs Node and a MiiJS checkout with
`npm install` done): `libmii/scripts/gen-test-vectors.sh build/libmii ../MiiJS`.

## PS Vita (.vpk)

Requires [VitaSDK](https://github.com/vitasdk/vdpm) and its `sdl2` package
(borealis uses SDL's input headers even with the GXM backend):

```sh
export VITASDK=/usr/local/vitasdk PATH=$VITASDK/bin:$PATH
vdpm install sdl2
cmake -S app -B build/vita -DPLATFORM_PSV=ON -DUSE_GXM=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/vita -j
# -> build/vita/mii_maker_vita.vpk
```

Install the `.vpk` with VitaShell. The app is marked "unsafe" (as borealis
requires), so enable unsafe homebrew in HENkaku settings.

## Desktop (fast UI iteration)

Linux needs GLFW's X11 dependencies and D-Bus headers, e.g. on Ubuntu:
`libgl-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libdbus-1-dev`.

```sh
cmake -S app -B build/desktop -DPLATFORM_DESKTOP=ON -DGLFW_BUILD_WAYLAND=OFF
cmake --build build/desktop -j
cd build/desktop && ./mii_maker_vita        # data goes to ./mii-data/
```

Keys: arrows navigate, Enter = ✕ (confirm), Esc = ○ (back), Ctrl+S = save.

## Regenerating assets

- Button glyph font: `pip install fonttools && python3 app/tools/make_button_font.py`
