#pragma once

#include <string>

// Where things live (desktop builds use ./data/ in place of ux0:data/):
//
//   ux0:data/mii/                  the Miis, shared with other apps and game
//       database.nfdb (+ .bak)     ports (see docs/SHARED_DATABASE.md)
//       export/                    Mii files written by "Export"
//       import/                    Mii files (and Mii QR code pictures) to
//                                  add with "Import"
//       qr/                        QR code pictures made by "Create QR Code"
//       wii/RFL_DB.dat             the first 100 Miis as the Wii's Mii database,
//                                  for ports of Wii games (with the user's own
//                                  RFL_Res.dat beside it)
//   ux0:data/mii-maker-vita/       this app's own files
//       author_id.bin              this console's Mii author id
//       icons.cache                drawn icons and thumbnails, in one file
//                                  (safe to delete)
//       dat/                       the user's resource files: AFLResHigh_2_3.dat,
//                                  FFLResHigh.dat, FFLResMiddle.dat, FFLBodyRes.dat
namespace paths {

const std::string& miiDir();
const std::string& appDir();
const std::string& datDir();
// The Miis as the Wii's Mii database (RFL_DB.dat), for Wii game ports;
// they also look here for the Wii's face resource (RFL_Res.dat)
const std::string& wiiDir();
const std::string& iconCache(); // drawn icons, one file; safe to delete
// Photo Studio's pictures: ux0:picture/MIIMAKER (the Photos app's folder)
const std::string& photoDir();

// Creates the folders above, and moves files an older version kept in
// ux0:data/mii/ (author_id.bin, *.dat) to their new places.
void prepare();

} // namespace paths
