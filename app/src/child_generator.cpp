#include "child_generator.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <vector>

namespace child {

#include "child_generator_tables.inc"

namespace {

std::mt19937& rng()
{
    static std::mt19937 r((uint32_t)std::chrono::steady_clock::now().time_since_epoch().count() ^ std::random_device{}());
    return r;
}

double (*gRandom)() = nullptr; // tests: MiiJS's Math.random sequence

// Math.floor(Math.random() * n)
int below(int n)
{
    if (gRandom)
        return (int)std::floor(gRandom() * n);
    return n <= 1 ? 0 : (int)std::uniform_int_distribution<int>(0, n - 1)(rng());
}

int clampi(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

// JS Math.floor of an integer division that may be negative
int floorDiv(int a, int b) { return (int)std::floor((double)a / (double)b); }

void setName(mii_charinfo_t* m, const char* utf8) { mii_name_set_utf8(m, utf8); }

// kidomatic(mii, hairGroupIndex) for the child flow (a hairstyle group given)
std::array<mii_charinfo_t, kStages> kidomatic(const mii_charinfo_t& mii, int hairGroup)
{
    // the grown-up stage's placement, and offsets back through the younger stages
    // (MiiJS counts the eyebrows' height from 0, where charinfo's starts at 3)
    const int browY = mii.eyebrow_y - 3;
    const int eyeBase = clampi(mii.eye_y + 2, 0, 18);
    const int browBase = clampi(browY + 2, 0, 15);
    const int mouthBase = clampi(mii.mouth_y - 2, 0, 18);
    const int eyeDelta = mii.eye_y - eyeBase, browDelta = browY - browBase, mouthDelta = mii.mouth_y - mouthBase;

    std::array<mii_charinfo_t, kStages> st;
    for (int i = 0; i < kStages; i++) {
        mii_charinfo_t s = mii;
        s.eye_y = (uint8_t)(floorDiv(eyeDelta * i, 5) + eyeBase);
        s.eyebrow_y = (uint8_t)(floorDiv(browDelta * i, 5) + browBase + 3);
        s.mouth_y = (uint8_t)(floorDiv(mouthDelta * i, 5) + mouthBase);
        s.nose_scale = (uint8_t)floorDiv(mii.nose_scale * i, 5);
        if (i < 4) {
            s.faceline_type = 9;
            s.eyebrow_color = 7;
        }
        // (the child flow always gives a hairstyle group: every stage)
        s.beard_type = 0;
        s.mustache_type = 0;
        s.faceline_wrinkle = 0;
        s.faceline_make = 0;
        s.glass_type = 0;
        s.hair_flip = 0;
        s.height = (uint8_t)std::floor(s.height / 5.0 * i);
        st[i] = s;
    }

    // Hairstyles: from the group's youngest age group up; at each later
    // stage a chance (1 in 3 for boys, 1 in 4 for girls) of staying in the
    // same age group, never the newborn's
    int ageGroup = 0;
    for (int h = 0; h < 4; h++) {
        const HairList& list = kHairGroups[hairGroup][ageGroup];
        const uint8_t type = list.types[below(list.count)];
        switch (h) {
        case 0: st[0].hair_type = type; break;
        case 1: st[1].hair_type = st[2].hair_type = type; break;
        case 2: st[3].hair_type = st[4].hair_type = type; break;
        case 3: st[5].hair_type = type; break;
        }
        if (h == 0 || below(st[0].gender == 0 ? 3 : 4) != 0)
            ageGroup = std::min(ageGroup + 1, 3);
    }
    return st;
}

} // namespace

void setRandomForTests(double (*random)()) { gRandom = random; }

std::array<mii_charinfo_t, kStages> generate(const mii_charinfo_t& parentA, const mii_charinfo_t& parentB,
    const std::array<mii_create_id_t, kStages>& ids)
{
    // parent0 the father (when there is one)
    mii_charinfo_t p0 = parentA, p1 = parentB;
    if (p0.gender != 0)
        std::swap(p0, p1);

    int coin[8];
    for (int& c : coin)
        c = below(2); // coin[1] is never used (as in the research)

    const mii_charinfo_t& main = coin[0] == 1 ? p1 : p0;
    mii_charinfo_t c = main; // most of the face comes from one parent
    c.type = 0;              // a normal Mii
    c.region_move = 0;

    const int gender = below(2);
    c.gender = (uint8_t)gender;
    if (gender == 0)
        setName(&c, kBoysNames[below((int)(sizeof(kBoysNames) / sizeof(kBoysNames[0])))]);
    else
        setName(&c, kGirlsNames[below((int)(sizeof(kGirlsNames) / sizeof(kGirlsNames[0])))]);
    const mii_charinfo_t& same = p0.gender == gender ? p0 : p1;

    // skin: a table of what two skin colors give
    const int s0 = kSkinTo3ds[p0.faceline_color % 10], s1 = kSkinTo3ds[p1.faceline_color % 10];
    std::vector<int> skins;
    for (int v : kSkinMixing[std::min(s0, s1)][std::max(s0, s1)])
        if (v != -1)
            skins.push_back(v);
    c.faceline_color = (uint8_t)(skins.empty() ? s0 : skins[below((int)skins.size())]);

    const int hairGroup = kHairGroupFor[same.hair_type % 132][gender];
    c.hair_color = coin[2] == 0 ? p0.hair_color : p1.hair_color;
    c.eyebrow_color = c.hair_color;
    c.faceline_type = same.faceline_type;
    c.eye_type = coin[3] == 0 ? p0.eye_type : p1.eye_type;
    c.eye_color = coin[4] == 0 ? p0.eye_color : p1.eye_color;
    c.eyebrow_type = same.eyebrow_type;
    c.nose_type = coin[5] == 0 ? p0.nose_type : p1.nose_type;
    c.mouth_type = coin[6] == 0 ? p0.mouth_type : p1.mouth_type;
    c.mouth_color = coin[7] == 0 ? p0.mouth_color : p1.mouth_color;
    c.mole_type = below(2) == 0 ? p0.mole_type : p1.mole_type;

    // the grown-up height and build, from one parent each
    const mii_charinfo_t& hp = below(2) == 0 ? p0 : p1;
    double height = (hp.height >> 3) * 1.4;
    height *= 1.4 * 1.4;
    height *= 1.4 * 1.4;
    c.height = (uint8_t)std::lround(std::min(std::max(height, 0.0), 127.0));
    const mii_charinfo_t& bp = below(2) == 0 ? p0 : p1;
    double build = std::trunc((bp.build + 1) / 4.0) + 48;
    for (int i = 0; i < 5; i++)
        build += (build - 64.0) * 0.2;
    c.build = (uint8_t)std::lround(std::min(std::max(build, 0.0), 127.0));

    // a favorite color from the personality groups a child of this gender has
    static const uint8_t kBoyColors[4] = { 2, 3, 5, 6 }, kGirlColors[4] = { 0, 1, 7, 8 };
    c.favorite_color = gender == 0 ? kBoyColors[below(4)] : kGirlColors[below(4)];

    std::array<mii_charinfo_t, kStages> stages = kidomatic(c, hairGroup);
    for (int i = 0; i < kStages; i++) {
        stages[i].create_id = ids[i];
        // keep every field in its range (the stages' arithmetic stays inside,
        // but the parents may be odd)
        for (int f = 0; f < MII_FIELD_COUNT; f++)
            mii_field_set(&stages[i], (mii_field_t)f, mii_field_get(&stages[i], (mii_field_t)f));
    }
    return stages;
}

} // namespace child
