#include "activities/editor_activity.hpp"

#include "mii_library.hpp"
#include "views/controls.hpp"
#include "views/mii_preview.hpp"

#include <mii/mii_editor.h>
#include <mii3d/mii3d.h>

#include <cmath>

using namespace brls::literals;

static const uint32_t kFavoriteSwatch[12] = {
    0xD21E14, 0xFF6E19, 0xFFD820, 0x78D220, 0x00782D, 0x0A48B4,
    0x3CAADE, 0xF55A7D, 0x7328AD, 0x483818, 0xE0E0E0, 0x181814,
};

static std::vector<uint8_t> gridItems(mii_grid_kind_t kind, int* columns)
{
    const mii_grid_t* g = mii_editor_grid(kind);
    *columns = g->columns;
    return std::vector<uint8_t>(g->items, g->items + g->count);
}

EditorActivity::EditorActivity(const mii_charinfo_t& start, OnSaved onSaved)
    : mii(start)
    , onSaved(std::move(onSaved))
{
}

brls::View* EditorActivity::makeTab(Fill fill)
{
    auto* scroll = new brls::ScrollingFrame();
    auto* box = new brls::Box(brls::Axis::COLUMN);
    box->setPadding(10, 30, 40, 30);
    fill(box);
    scroll->setContentView(box);
    return scroll;
}

// ---- grid icons --------------------------------------------------------
// Like the Switch editor, each style tile shows that style in the Mii's
// current colors. Shapes are drawn as small heads framed on the part;
// flat parts (eyes, brows, mouth, mustache, glasses) as the part alone.

static mii_charinfo_t iconBase(const mii_charinfo_t& cur)
{
    mii_charinfo_t m;
    mii_charinfo_init_default(&m, cur.gender);
    m.faceline_color = cur.faceline_color;
    m.hair_color = cur.hair_color;
    m.eyebrow_color = cur.eyebrow_color;
    m.beard_color = cur.beard_color;
    return m;
}

static OptionGrid::IconOf iconFor(mii_field_t field)
{
    using Spec = HeadRenderer::Spec;
    auto head = [](float zoom, float centerY, void (*set)(mii_charinfo_t&, uint8_t, const mii_charinfo_t&)) {
        return OptionGrid::IconOf([=](uint8_t v, const mii_charinfo_t& cur, Spec* spec) {
            spec->mii = iconBase(cur);
            set(spec->mii, v, cur);
            spec->zoom = zoom;
            spec->centerY = centerY;
        });
    };
    auto part = [](mii3d_icon_part_t which, mii_field_t f) {
        return OptionGrid::IconOf([=](uint8_t v, const mii_charinfo_t& cur, Spec* spec) {
            spec->mii = cur;
            mii_field_set(&spec->mii, f, v);
            spec->icon = which;
        });
    };
    switch (field) {
    case MII_FIELD_HAIR_TYPE:
        return head(1.12f, 2.0f, [](mii_charinfo_t& m, uint8_t v, const mii_charinfo_t& c) {
            m.hair_type = v;
            m.hair_flip = c.hair_flip;
        });
    case MII_FIELD_FACELINE_TYPE:
        return head(1.0f, 0.0f, [](mii_charinfo_t& m, uint8_t v, const mii_charinfo_t&) {
            m.faceline_type = v;
            m.hair_type = 30; // bald, so the shape shows
        });
    case MII_FIELD_FACELINE_WRINKLE:
        return head(2.0f, -6.0f, [](mii_charinfo_t& m, uint8_t v, const mii_charinfo_t&) {
            m.faceline_wrinkle = v;
            m.hair_type = 30;
        });
    case MII_FIELD_FACELINE_MAKE:
        return head(1.8f, -6.0f, [](mii_charinfo_t& m, uint8_t v, const mii_charinfo_t&) {
            m.faceline_make = v;
            m.hair_type = 30;
        });
    case MII_FIELD_NOSE_TYPE:
        return head(3.0f, -9.0f, [](mii_charinfo_t& m, uint8_t v, const mii_charinfo_t&) { m.nose_type = v; });
    case MII_FIELD_BEARD_TYPE:
        return head(1.7f, -14.0f, [](mii_charinfo_t& m, uint8_t v, const mii_charinfo_t&) {
            m.beard_type = v;
            m.hair_type = 30; // keep the hair out of the way
        });
    case MII_FIELD_EYE_TYPE:      return part(MII3D_ICON_EYES, field);
    case MII_FIELD_EYEBROW_TYPE:  return part(MII3D_ICON_EYEBROWS, field);
    case MII_FIELD_MOUTH_TYPE:    return part(MII3D_ICON_MOUTH, field);
    case MII_FIELD_MUSTACHE_TYPE: return part(MII3D_ICON_MUSTACHE, field);
    case MII_FIELD_GLASS_TYPE:    return part(MII3D_ICON_GLASSES, field);
    default:                      return nullptr;
    }
}

// Adds a parts grid, a color palette and sliders: the common shape of most
// categories.
struct Section {
    static constexpr float kIconCell = 76.0f;

    static void parts(EditorActivity* self, brls::Box* box, mii_charinfo_t* mii, mii_field_t field,
        mii_grid_kind_t kind, std::function<void()> changed)
    {
        int cols;
        std::vector<uint8_t> items = gridItems(kind, &cols);
        box->addView(new OptionGrid(mii, field, items, cols, kIconCell, nullptr, nullptr, changed, iconFor(field)));
    }
    static void range(brls::Box* box, mii_charinfo_t* mii, mii_field_t field, int cols,
        std::function<void()> changed)
    {
        OptionGrid::IconOf icon = iconFor(field);
        box->addView(new OptionGrid(mii, field, OptionGrid::range(field), cols, icon ? kIconCell : 60, nullptr,
            nullptr, changed, icon));
    }
    static void colors(brls::Box* box, mii_charinfo_t* mii, mii_field_t field, std::function<void()> changed)
    {
        int cols;
        std::vector<uint8_t> items = gridItems(MII_GRID_COMMON_COLOR, &cols);
        box->addView(new OptionGrid(mii, field, items, cols, 40,
            [](uint8_t v) { return mii_common_color_rgb(v); }, nullptr, changed));
    }
};

brls::View* EditorActivity::profileTab()
{
    return makeTab([this](brls::Box* box) {
        auto changed = [this] { this->changed(); };

        box->addView(makeSectionTitle("editor/nickname"_i18n));
        auto* name = new brls::Button();
        char buf[64];
        mii_name_get_utf8(&mii, buf, sizeof(buf));
        name->setText(buf);
        name->setWidth(360);
        name->registerClickAction([this, name](brls::View*) {
            char cur[64];
            mii_name_get_utf8(&mii, cur, sizeof(cur));
            brls::Application::getImeManager()->openForText(
                [this, name](std::string text) {
                    if (text.empty())
                        return;
                    mii_name_set_utf8(&mii, text.c_str());
                    char out[64];
                    mii_name_get_utf8(&mii, out, sizeof(out));
                    name->setText(out);
                    this->changed();
                },
                "editor/nickname"_i18n, "", MII_NAME_LENGTH, cur);
            return true;
        });
        box->addView(name);

        box->addView(makeSectionTitle("editor/gender"_i18n));
        box->addView(new OptionGrid(&mii, MII_FIELD_GENDER, { 0, 1 }, 2, 56, nullptr,
            [](uint8_t v) { return v ? "editor/female"_i18n : "editor/male"_i18n; }, changed));

        box->addView(makeSectionTitle("editor/favorite_color"_i18n));
        box->addView(new OptionGrid(&mii, MII_FIELD_FAVORITE_COLOR, OptionGrid::range(MII_FIELD_FAVORITE_COLOR), 6, 44,
            [](uint8_t v) { return kFavoriteSwatch[v % 12]; }, nullptr, changed));
    });
}

brls::View* EditorActivity::faceTab()
{
    return makeTab([this](brls::Box* box) {
        auto changed = [this] { this->changed(); };
        box->addView(makeSectionTitle("editor/face_shape"_i18n));
        Section::range(box, &mii, MII_FIELD_FACELINE_TYPE, 6, changed);
        box->addView(makeSectionTitle("editor/skin_color"_i18n));
        int cols;
        std::vector<uint8_t> skin = gridItems(MII_GRID_FACELINE_COLOR, &cols);
        box->addView(new OptionGrid(&mii, MII_FIELD_FACELINE_COLOR, skin, cols, 44,
            [](uint8_t v) { return mii_faceline_color_rgb(v); }, nullptr, changed));
        box->addView(makeSectionTitle("editor/wrinkles"_i18n));
        Section::range(box, &mii, MII_FIELD_FACELINE_WRINKLE, 6, changed);
        box->addView(makeSectionTitle("editor/makeup"_i18n));
        Section::range(box, &mii, MII_FIELD_FACELINE_MAKE, 6, changed);
    });
}

brls::View* EditorActivity::hairTab()
{
    return makeTab([this](brls::Box* box) {
        auto changed = [this] { this->changed(); };
        box->addView(makeSectionTitle("editor/style"_i18n));
        Section::parts(this, box, &mii, MII_FIELD_HAIR_TYPE, MII_GRID_HAIR, changed);
        box->addView(makeSectionTitle("editor/color"_i18n));
        Section::colors(box, &mii, MII_FIELD_HAIR_COLOR, changed);
        auto* flip = new brls::BooleanCell();
        flip->init("editor/flip_part"_i18n, mii.hair_flip, [this](bool on) {
            mii.hair_flip = on;
            this->changed();
        });
        box->addView(flip);
    });
}

brls::View* EditorActivity::eyebrowTab()
{
    return makeTab([this](brls::Box* box) {
        auto changed = [this] { this->changed(); };
        box->addView(makeSectionTitle("editor/style"_i18n));
        Section::parts(this, box, &mii, MII_FIELD_EYEBROW_TYPE, MII_GRID_EYEBROW, changed);
        box->addView(makeSectionTitle("editor/color"_i18n));
        Section::colors(box, &mii, MII_FIELD_EYEBROW_COLOR, changed);
        box->addView(makeSectionTitle("editor/adjust"_i18n));
        box->addView(new FieldSlider(&mii, MII_FIELD_EYEBROW_SCALE, "editor/size"_i18n, changed));
        box->addView(new FieldSlider(&mii, MII_FIELD_EYEBROW_ASPECT, "editor/thickness"_i18n, changed));
        box->addView(new FieldSlider(&mii, MII_FIELD_EYEBROW_ROTATE, "editor/rotation"_i18n, changed));
        box->addView(new FieldSlider(&mii, MII_FIELD_EYEBROW_X, "editor/spacing"_i18n, changed));
        box->addView(new FieldSlider(&mii, MII_FIELD_EYEBROW_Y, "editor/height"_i18n, changed));
    });
}

brls::View* EditorActivity::eyeTab()
{
    return makeTab([this](brls::Box* box) {
        auto changed = [this] { this->changed(); };
        box->addView(makeSectionTitle("editor/style"_i18n));
        Section::parts(this, box, &mii, MII_FIELD_EYE_TYPE, MII_GRID_EYE, changed);
        box->addView(makeSectionTitle("editor/color"_i18n));
        Section::colors(box, &mii, MII_FIELD_EYE_COLOR, changed);
        box->addView(makeSectionTitle("editor/adjust"_i18n));
        box->addView(new FieldSlider(&mii, MII_FIELD_EYE_SCALE, "editor/size"_i18n, changed));
        box->addView(new FieldSlider(&mii, MII_FIELD_EYE_ASPECT, "editor/squash"_i18n, changed));
        box->addView(new FieldSlider(&mii, MII_FIELD_EYE_ROTATE, "editor/rotation"_i18n, changed));
        box->addView(new FieldSlider(&mii, MII_FIELD_EYE_X, "editor/spacing"_i18n, changed));
        box->addView(new FieldSlider(&mii, MII_FIELD_EYE_Y, "editor/height"_i18n, changed));
    });
}

brls::View* EditorActivity::noseTab()
{
    return makeTab([this](brls::Box* box) {
        auto changed = [this] { this->changed(); };
        box->addView(makeSectionTitle("editor/style"_i18n));
        Section::parts(this, box, &mii, MII_FIELD_NOSE_TYPE, MII_GRID_NOSE, changed);
        box->addView(makeSectionTitle("editor/adjust"_i18n));
        box->addView(new FieldSlider(&mii, MII_FIELD_NOSE_SCALE, "editor/size"_i18n, changed));
        box->addView(new FieldSlider(&mii, MII_FIELD_NOSE_Y, "editor/height"_i18n, changed));
    });
}

brls::View* EditorActivity::mouthTab()
{
    return makeTab([this](brls::Box* box) {
        auto changed = [this] { this->changed(); };
        box->addView(makeSectionTitle("editor/style"_i18n));
        Section::parts(this, box, &mii, MII_FIELD_MOUTH_TYPE, MII_GRID_MOUTH, changed);
        box->addView(makeSectionTitle("editor/color"_i18n));
        Section::colors(box, &mii, MII_FIELD_MOUTH_COLOR, changed);
        box->addView(makeSectionTitle("editor/adjust"_i18n));
        box->addView(new FieldSlider(&mii, MII_FIELD_MOUTH_SCALE, "editor/size"_i18n, changed));
        box->addView(new FieldSlider(&mii, MII_FIELD_MOUTH_ASPECT, "editor/thickness"_i18n, changed));
        box->addView(new FieldSlider(&mii, MII_FIELD_MOUTH_Y, "editor/height"_i18n, changed));
    });
}

brls::View* EditorActivity::facialHairTab()
{
    return makeTab([this](brls::Box* box) {
        auto changed = [this] { this->changed(); };
        box->addView(makeSectionTitle("editor/mustache"_i18n));
        Section::range(box, &mii, MII_FIELD_MUSTACHE_TYPE, 6, changed);
        box->addView(makeSectionTitle("editor/beard"_i18n));
        Section::range(box, &mii, MII_FIELD_BEARD_TYPE, 6, changed);
        box->addView(makeSectionTitle("editor/color"_i18n));
        Section::colors(box, &mii, MII_FIELD_BEARD_COLOR, changed);
        box->addView(makeSectionTitle("editor/adjust"_i18n));
        box->addView(new FieldSlider(&mii, MII_FIELD_MUSTACHE_SCALE, "editor/size"_i18n, changed));
        box->addView(new FieldSlider(&mii, MII_FIELD_MUSTACHE_Y, "editor/height"_i18n, changed));
    });
}

brls::View* EditorActivity::glassesTab()
{
    return makeTab([this](brls::Box* box) {
        auto changed = [this] { this->changed(); };
        box->addView(makeSectionTitle("editor/style"_i18n));
        Section::parts(this, box, &mii, MII_FIELD_GLASS_TYPE, MII_GRID_GLASS, changed);
        box->addView(makeSectionTitle("editor/color"_i18n));
        Section::colors(box, &mii, MII_FIELD_GLASS_COLOR, changed);
        box->addView(makeSectionTitle("editor/adjust"_i18n));
        box->addView(new FieldSlider(&mii, MII_FIELD_GLASS_SCALE, "editor/size"_i18n, changed));
        box->addView(new FieldSlider(&mii, MII_FIELD_GLASS_Y, "editor/height"_i18n, changed));
    });
}

brls::View* EditorActivity::moleTab()
{
    return makeTab([this](brls::Box* box) {
        auto changed = [this] { this->changed(); };
        auto* on = new brls::BooleanCell();
        on->init("editor/mole"_i18n, mii.mole_type, [this](bool v) {
            mii.mole_type = v;
            this->changed();
        });
        box->addView(on);
        box->addView(makeSectionTitle("editor/adjust"_i18n));
        box->addView(new FieldSlider(&mii, MII_FIELD_MOLE_SCALE, "editor/size"_i18n, changed));
        box->addView(new FieldSlider(&mii, MII_FIELD_MOLE_X, "editor/horizontal"_i18n, changed));
        box->addView(new FieldSlider(&mii, MII_FIELD_MOLE_Y, "editor/vertical"_i18n, changed));
    });
}

brls::View* EditorActivity::bodyTab()
{
    return makeTab([this](brls::Box* box) {
        auto changed = [this] { this->changed(); };
        box->addView(makeSectionTitle("editor/body"_i18n));
        box->addView(new FieldSlider(&mii, MII_FIELD_HEIGHT, "editor/height"_i18n, changed));
        box->addView(new FieldSlider(&mii, MII_FIELD_BUILD, "editor/build"_i18n, changed));
    });
}

brls::View* EditorActivity::createContentView()
{
    auto* root = new brls::Box(brls::Axis::ROW);

    auto* preview = new MiiPreview(&mii, 256);
    preview->setWidth(270);
    preview->setMargins(16, 0, 16, 16);
    root->addView(preview);

    auto* tabs = new brls::TabFrame();
    tabs->setGrow(1);
    tabs->addTab("editor/tabs/profile"_i18n, [this] { return profileTab(); });
    tabs->addTab("editor/tabs/face"_i18n, [this] { return faceTab(); });
    tabs->addTab("editor/tabs/hair"_i18n, [this] { return hairTab(); });
    tabs->addTab("editor/tabs/eyebrows"_i18n, [this] { return eyebrowTab(); });
    tabs->addTab("editor/tabs/eyes"_i18n, [this] { return eyeTab(); });
    tabs->addTab("editor/tabs/nose"_i18n, [this] { return noseTab(); });
    tabs->addTab("editor/tabs/mouth"_i18n, [this] { return mouthTab(); });
    tabs->addTab("editor/tabs/facial_hair"_i18n, [this] { return facialHairTab(); });
    tabs->addTab("editor/tabs/glasses"_i18n, [this] { return glassesTab(); });
    tabs->addTab("editor/tabs/mole"_i18n, [this] { return moleTab(); });
    tabs->addTab("editor/tabs/body"_i18n, [this] { return bodyTab(); });
    root->addView(tabs);

    auto* frame = new brls::AppletFrame(root);
    frame->setTitle("editor/title"_i18n);
    frame->registerAction("editor/save"_i18n, brls::BUTTON_START, [this](brls::View*) { return save(); });
    // Keyboard shortcut for desktop builds (no START key there).
    frame->registerAction(brls::BrlsKeyCombination(brls::BRLS_KBD_KEY_S, brls::BRLS_KBD_MODIFIER_CTRL),
        [this](brls::View*) { return save(); });
    frame->registerAction("hints/back"_i18n, brls::BUTTON_B, [this](brls::View*) { return close(); });
    // L / R turn the preview, in the steps the 3D renderer caches well.
    frame->registerAction("editor/turn_left"_i18n, brls::BUTTON_LB, [preview](brls::View*) {
        preview->setYaw(std::fmax(-60.0f, preview->getYaw() - 20.0f));
        return true;
    }, false, true);
    frame->registerAction("editor/turn_right"_i18n, brls::BUTTON_RB, [preview](brls::View*) {
        preview->setYaw(std::fmin(60.0f, preview->getYaw() + 20.0f));
        return true;
    }, false, true);
    return frame;
}

bool EditorActivity::save()
{
    if (mii_verify(&mii, 0) != MII_FIELD_COUNT) {
        brls::Application::notify("editor/invalid"_i18n);
        return true;
    }
    if (MiiLibrary::instance().put(&mii) < 0) {
        brls::Application::notify("editor/save_failed"_i18n);
        return true;
    }
    dirty = false;
    auto cb = onSaved;
    brls::Application::popActivity(brls::TransitionAnimation::FADE, [cb] {
        if (cb)
            cb();
    });
    return true;
}

bool EditorActivity::close()
{
    if (!dirty) {
        brls::Application::popActivity();
        return true;
    }
    auto* dialog = new brls::Dialog("editor/discard"_i18n);
    dialog->addButton("hints/cancel"_i18n, [] {});
    dialog->addButton("editor/discard_yes"_i18n, [] { brls::Application::popActivity(); });
    dialog->open();
    return true;
}
