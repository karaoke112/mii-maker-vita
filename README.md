# Mii Maker Vita

### This readme is written by a human.

Mii Maker Vita is a Mii Maker clone for PlayStation Vita! You can, of course, make your own Miis, but also send your Miis and receive other people's Miis! It is designed to mimic the Nintendo Switch Mii Maker in both aesthetics and function. Miis are rendered with the official renderer and saved in the official format.

You can store up to 1000 Miis in the Switch-era format, 100 in the 3DS/WiiU format, and 100 in the Wii format. Miis can be easily copied between formats by picking them up with triangle and switching tabs with the shoulder buttons. The 3DS tab directly modifies Azahar's Mii database, ~~allowing for cool stuff and also things~~.


# Full Feature Set

- **Creation**
    - Create Mii from scratch or from a look-alike
    - Editor and customization both have full parity with the Switch, 3DS/WiiU, and Wii; depending on context
    - Kid-o-matic and Child Generation functionality (from [Infinimii](https://github.com/Stewared/InfiniMii))
    - View your Mii with all 60 expressions from Miitomo
    - View your Mii with all 171 poses from Mario Kart 8
- **Organization**
    - View all your Miis as heads and reorder them as you like
    - Sort and filter options
    - Miis marked as Favorites have a red star
- **Import & Export**
    - `.charinfo` formatted files
    - Official QR code formats
    - Batch sending and receiving via ad-hoc connectivity
- **Settings**
    - Set default skin tone
    - Set app theme
    - Switch music track
    - Change music and sfx volume
    - Change QR code format
    - Allow or disallow Look-Alike Mii generation to be insane

# Installation

1. Have [libshacccg](https://github.com/Rinnegatamante/ShaRKBR33D) installed
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

My hope is that other apps start to support this. Wii games could read the Wii-formatted Mii catalog, other apps could read the Switch-formatted Miis, etc. I have the Miis stored in a separate folder to try and have them be independent from any one piece of software.
