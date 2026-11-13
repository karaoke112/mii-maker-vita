#pragma once

#include <mii/mii.h>

#include <array>
#include <cstdint>

// A child of two Miis at six stages of life, as InfiniMii makes it: a port of
// MiiJS's makeMiiChild and kidomatic (ISC License, https://github.com/Stewared/MiiJS),
// which follow Tomodachi Life's child generation (Tomodachi Life itself
// uses separate models instead of heights; the growing height is MiiJS's).
//
// One parent gives most of the face; each of eye type and color, hair color,
// nose, mouth and lip color comes from either parent; the face shape and
// eyebrows from the parent of the child's gender; the skin color from a
// table of what two skin colors give. Stages 0-5 run from newborn to grown
// up: features drift from a child's placement to the grown-up's, the height
// grows, and the hairstyle moves through age groups of hairstyles that
// follow the matching parent's style. Beards, mustaches, glasses, wrinkles,
// makeup and a flipped parting are left out at every stage, as MiiJS does.
namespace child {

constexpr int kStages = 6;

// Each stage gets the create ID `ids[i]` (so each can be saved on its own).
std::array<mii_charinfo_t, kStages> generate(const mii_charinfo_t& parentA, const mii_charinfo_t& parentB,
    const std::array<mii_create_id_t, kStages>& ids);

// Tests: draw from `random` (as JavaScript's Math.random, [0, 1)) instead.
void setRandomForTests(double (*random)());

} // namespace child
