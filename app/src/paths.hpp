#pragma once

#include <string>

// Where things live (desktop builds use ./data/ in place of ux0:data/):
//
//   ux0:data/mii/                  the Miis, shared with other apps and game
//       database.nfdb (+ .bak)     ports (see docs/SHARED_DATABASE.md)
//       export/                    Mii files written by "Export"
//   ux0:data/mii-maker-vita/       this app's own files
//       author_id.bin              this console's Mii author id
//       dat/                       the user's resource files: AFLResHigh_2_3.dat,
//                                  FFLResHigh.dat, FFLResMiddle.dat, FFLBodyRes.dat
namespace paths {

const std::string& miiDir();
const std::string& appDir();
const std::string& datDir();

// Creates the folders above, and moves files an older version kept in
// ux0:data/mii/ (author_id.bin, *.dat) to their new places.
void prepare();

} // namespace paths
