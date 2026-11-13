# Mii Maker Vita

### This readme is written by a human.

Mii Maker Vita is a Mii Maker clone for PlayStation Vita! You can, of course, make your own Miis, but also send your Miis and receive other people's Miis! It is designed to mimic the Nintendo Switch Mii Maker in both aesthetics and function. Miis are rendered with the official renderer and saved in the official format.

You can store 200 Miis (to be increased), however only the first 100 will be backed up and stored in Wii format. Simply reorder your Miis to keep your favorites in the Wii catalog. This only matters if you have other software that supports reading Miis from `ux0:`, and also needs them in native Wii format.


# Full Feature Set

- **Creation**
    - Create Mii from scratch or from a look-alike
    - Editor and customization both have full parity with the Switch
    - Kid-o-matic and Child Generation functionality (from [Infinimii](https://github.com/Stewared/InfiniMii))
    - View your Mii with all 60 expressions from Miitomo
    - View your Mii with all 171 poses from Mario Kart 8
- **Organization**
    - View all your Miis as heads and reorder them as you like
- **Import & Export**
    - `.charinfo` formatted files
    - Official QR code formats
    - Ad-hoc connectivity
- **Settings**
    - Set default skin tone
    - Set app theme
    - Change music
    - Change QR code format
    - Allow or disallow Look-Alike Mii generation to be insane

# Installation

1. Have [libshacccg](https://github.com/AnimMouse/SceShaccCg) installed
2. Download `mii-maker-vita.vpk` and `place-contents-on-root.zip` from [Releases
](https://github.com/karaoke112/mii-maker-vita/releases)
3. Copy `mii-maker-vita.vpk` and install it to your Vita using [VitaShell
](https://github.com/theofficialflow/vitashell)
4. Extract the `data` folder from `place-contents-on-root.zip` and place it on the root of `ux0:`

# Unlicensed Files

Unlicensed files are not stored in the repo, and you must obtain them yourself in the event they are removed from the Releases page.

|  File | Source | What is it | Path |
| --- | --- | --- | --- |
| `AFLResHigh_2_3.dat` | Miitomo | Miitomo's Mii head renderer | `ux0:data/mii-maker-vita/dat` |
| `FFLBodyRes.dat` | Mario Kart 8 (Wii U)| Wii U's Mii static body renderer | `ux0:data/mii-maker-vita/dat` |
| `MiiBodyMiddle.bfres` | Mario Kart 8 (Wii U)| Wii U's Mii body and pose renderer | `ux0:data/mii-maker-vita/dat` |
| `qrkey.txt` | 3DS & Switch firmware | Mii QR code decryption keys | `ux0:data/mii-maker-vita/dat` |
| `mii_channel.ogg` | Wii BIOS| Wii's Mii Channel music | `ux0:data/mii-maker-vita/music` |
| `mii_maker_3ds.ogg` | 3DS system files| 3DS's Mii Maker music| `ux0:data/mii-maker-vita/music` |
| `mii_maker_wiiu.ogg` | Wii U system files|  Wii U's Mii Maker music| `ux0:data/mii-maker-vita/music` |

# Connectivity

My hope is that other apps start to support this. Any Wii games ported to Vita could in the future make use of the Miis on the system. I have the Miis stored in a separate folder to try and have them be independent from any one piece of software.
