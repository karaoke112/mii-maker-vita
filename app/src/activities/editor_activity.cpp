#include "activities/editor_activity.hpp"

#include "mii_library.hpp"
#include "ui_theme.hpp"
#include "views/controls.hpp"
#include "views/icon.hpp"
#include "views/mii_preview.hpp"

#include <mii/mii_editor.h>
#include <mii3d/mii3d.h>

#include <cmath>

using namespace brls::literals;

static std::vector<uint8_t> gridItems(mii_grid_kind_t kind, int* columns)
{
    const mii_grid_t* g = mii_editor_grid(kind);
    *columns = g->columns;
    return std::vector<uint8_t>(g->items, g->items + g->count);
}

static NVGcolor rgb(uint32_t c)
{
    return nvgRGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

// ---- shared layout -----------------------------------------------------

// The preview on the left and `right` beside it, in a frame with no title
// bar and the button hints along the bottom.
static brls::AppletFrame* editorFrame(EditorSession& s, brls::View* right, MiiPreview** previewOut,
    MiiPreview::Framing framing = MiiPreview::Framing::BUST)
{
    auto* root = new brls::Box(brls::Axis::ROW);

    auto* preview = new MiiPreview(&s.mii, 384);
    preview->setStage(true);
    preview->setFraming(framing);
    preview->setYawSource(&s.yaw);
    preview->setWidth(520);
    preview->setGrow(0);
    root->addView(preview);

    right->setGrow(1);
    root->addView(right);

    auto* frame = new brls::AppletFrame(root);
    frame->setHeaderVisibility(brls::Visibility::GONE);
    if (previewOut)
        *previewOut = preview;
    return frame;
}

// ---- the menu ----------------------------------------------------------

// A row of the menu: a pictogram and a name, with thin rules above/below.
class MenuRow : public brls::Box
{
  public:
    MenuRow(const char* iconName, const std::string& text, bool topRule)
        : brls::Box(brls::Axis::ROW)
        , topRule(topRule)
    {
        this->setHeight(64);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setPadding(0, 12, 0, 14);
        this->setFocusable(true);
        this->setHighlightCornerRadius(4);
        icon = new IconView(iconName ? iconName : "", 40, ui::kText);
        if (!iconName)
            icon->setVisibility(brls::Visibility::GONE);
        this->addView(icon);
        label = new brls::Label();
        label->setText(text);
        label->setFontSize(24);
        label->setMarginLeft(16);
        label->setGrow(1);
        this->addView(label);
        this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
    }

    IconView* iconView() { return icon; }

    // a value shown at the right end (the nickname)
    brls::Label* addValue()
    {
        auto* v = new brls::Label();
        v->setFontSize(24);
        v->setTextColor(ui::kAccent);
        v->setHorizontalAlign(brls::HorizontalAlign::RIGHT);
        this->addView(v);
        return v;
    }


    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
        brls::FrameContext* ctx) override
    {
        nvgBeginPath(vg);
        if (topRule)
            nvgRect(vg, x, y, width, 1.5f);
        nvgRect(vg, x, y + height - 1.5f, width, 1.5f);
        nvgFillColor(vg, ui::kRule);
        nvgFill(vg);
        brls::Box::draw(vg, x, y, width, height, style, ctx);
    }

  private:
    bool topRule;
    IconView* icon;
    brls::Label* label;
};

struct MenuEntry {
    Category category;
    const char* icon; // resources/icons/<icon>.png
    const char* key;
};

static const MenuEntry kMenu[] = {
    { Category::FACE, "face", "editor/menu/face" },
    { Category::HAIR, "hair", "editor/menu/hair" },
    { Category::EYEBROWS, "eyebrows", "editor/menu/eyebrows" },
    { Category::EYES, "eyes", "editor/menu/eyes" },
    { Category::NOSE, "nose", "editor/menu/nose" },
    { Category::MOUTH, "mouth", "editor/menu/mouth" },
    { Category::FACIAL_HAIR, "facial_hair", "editor/menu/facial_hair" },
    { Category::MOLE, "mole", "editor/menu/mole" },
    { Category::STYLE, "style", "editor/menu/style" },
    { Category::BODY, "body", "editor/menu/body" },
    { Category::GLASSES, "glasses", "editor/menu/glasses" },
    { Category::FAVORITE_COLOR, "star", "editor/menu/favorite_color" },
};

EditorActivity::EditorActivity(const mii_charinfo_t& start, OnSaved onSaved)
    : session(std::make_shared<EditorSession>())
    , onSaved(std::move(onSaved))
{
    session->mii = start;
}

brls::View* EditorActivity::createContentView()
{
    auto* menu = new brls::Box(brls::Axis::COLUMN);
    menu->setPadding(36, 40, 0, 30);

    std::shared_ptr<EditorSession> s = session;
    const int n = (int)(sizeof(kMenu) / sizeof(kMenu[0]));
    MenuRow* star = nullptr;
    std::vector<MenuRow*> rows; // in menu order: two per line
    for (int i = 0; i < n; i += 2) {
        auto* line = new brls::Box(brls::Axis::ROW);
        for (int j = i; j < i + 2 && j < n; j++) {
            const MenuEntry& e = kMenu[j];
            auto* row = new MenuRow(e.icon, brls::getStr(e.key), i == 0);
            row->setGrow(1);
            row->setWidthPercentage(50);
            if (j > i)
                row->setMarginLeft(28);
            const Category cat = e.category;
            row->registerClickAction([s, cat](brls::View*) {
                brls::Application::pushActivity(new CategoryActivity(s, cat), brls::TransitionAnimation::NONE);
                return true;
            });
            if (e.category == Category::FAVORITE_COLOR)
                star = row;
            line->addView(row);
            rows.push_back(row);
        }
        menu->addView(line);
    }
    // the star shows the favorite color
    if (star) {
        IconView* g = star->iconView();
        g->setOverlay("star_outline", ui::kText);
        g->setColor(rgb(mii_favorite_color_rgb(session->mii.favorite_color)));
        favoriteStar = g;
    }

    auto* nameRow = new MenuRow(nullptr, "editor/nickname"_i18n, false);
    brls::Label* nameValue = nameRow->addValue();
    auto showName = [s, nameValue] {
        char buf[64];
        mii_name_get_utf8(&s->mii, buf, sizeof(buf));
        nameValue->setText(buf);
    };
    showName();
    nameRow->registerClickAction([s, showName](brls::View*) {
        char cur[64];
        mii_name_get_utf8(&s->mii, cur, sizeof(cur));
        brls::Application::getImeManager()->openForText(
            [s, showName](std::string text) {
                if (text.empty())
                    return;
                mii_name_set_utf8(&s->mii, text.c_str());
                showName();
                s->dirty = true;
            },
            "editor/nickname"_i18n, "", MII_NAME_LENGTH, cur);
        return true;
    });
    menu->addView(nameRow);
    // Up / down stay in the same column (borealis would otherwise return
    // to whichever column of a line was focused last).
    for (size_t k = 0; k < rows.size(); k++) {
        if (k >= 2)
            rows[k]->setCustomNavigationRoute(brls::FocusDirection::UP, rows[k - 2]);
        rows[k]->setCustomNavigationRoute(brls::FocusDirection::DOWN, k + 2 < rows.size() ? rows[k + 2] : nameRow);
    }
    nameRow->setCustomNavigationRoute(brls::FocusDirection::UP, rows[rows.size() - 2]);

    auto* doneLine = new brls::Box(brls::Axis::ROW);
    doneLine->setJustifyContent(brls::JustifyContent::CENTER);
    doneLine->setMarginTop(34);
    auto* done = new brls::Button();
    done->setStyle(&brls::BUTTONSTYLE_PRIMARY);
    done->setText("editor/done"_i18n);
    done->setFontSize(26);
    done->setDimensions(320, 72);
    done->setCornerRadius(6);
    done->registerClickAction([this](brls::View*) { return save(); });
    doneLine->addView(done);
    menu->addView(doneLine);

    MiiPreview* preview = nullptr;
    brls::AppletFrame* frame = editorFrame(*session, menu, &preview);
    frame->registerAction("", brls::BUTTON_BACK, [](brls::View*) {
        MiiPreview::showTimings = !MiiPreview::showTimings;
        return true;
    }, true);
    frame->registerAction("editor/save"_i18n, brls::BUTTON_START, [this](brls::View*) { return save(); }, true);
    // Keyboard shortcut for desktop builds (no START key there).
    frame->registerAction(brls::BrlsKeyCombination(brls::BRLS_KBD_KEY_S, brls::BRLS_KBD_MODIFIER_CTRL),
        [this](brls::View*) { return save(); });
    frame->registerAction("hints/back"_i18n, brls::BUTTON_B, [this](brls::View*) { return close(); });
    return frame;
}

void EditorActivity::onResume()
{
    // the favorite color may have changed on its page
    if (favoriteStar)
        favoriteStar->setColor(rgb(mii_favorite_color_rgb(session->mii.favorite_color)));
}

bool EditorActivity::save()
{
    mii_charinfo_t& mii = session->mii;
    if (mii_verify(&mii, 0) != MII_FIELD_COUNT) {
        brls::Application::notify("editor/invalid"_i18n);
        return true;
    }
    if (MiiLibrary::instance().put(&mii) < 0) {
        brls::Application::notify("editor/save_failed"_i18n);
        return true;
    }
    session->dirty = false;
    auto cb = onSaved;
    brls::Application::popActivity(brls::TransitionAnimation::NONE, [cb] {
        if (cb)
            cb();
    });
    return true;
}

bool EditorActivity::close()
{
    if (!session->dirty) {
        brls::Application::popActivity(brls::TransitionAnimation::NONE);
        return true;
    }
    auto* dialog = new brls::Dialog("editor/discard"_i18n);
    dialog->addButton("hints/cancel"_i18n, [] {});
    dialog->addButton("editor/discard_yes"_i18n, [] { brls::Application::popActivity(brls::TransitionAnimation::NONE); });
    dialog->open();
    return true;
}

// ---- tile icons --------------------------------------------------------
// Like the Switch 2 editor, style tiles are drawn in neutral colors (black
// hair and lines, light skin) so the shapes read clearly; eyes and lips
// keep the Mii's own colors.

static mii_charinfo_t iconBase(const mii_charinfo_t& cur)
{
    mii_charinfo_t m;
    mii_charinfo_init_default(&m, cur.gender);
    m.faceline_color = 0;
    m.hair_color = 0;
    m.eyebrow_color = 0;
    m.beard_color = 0;
    m.glass_color = 0;
    m.eye_color = cur.eye_color;
    m.mouth_color = cur.mouth_color;
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
            spec->mii = iconBase(cur);
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
    case MII_FIELD_MOLE_TYPE:
        return head(2.2f, -6.0f, [](mii_charinfo_t& m, uint8_t v, const mii_charinfo_t& c) {
            m.mole_type = v; // where the Mii's mole is
            m.mole_scale = c.mole_scale;
            m.mole_x = c.mole_x;
            m.mole_y = c.mole_y;
            m.hair_type = 30;
        });
    case MII_FIELD_EYE_TYPE:      return part(MII3D_ICON_EYE, field);
    case MII_FIELD_EYEBROW_TYPE:  return part(MII3D_ICON_EYEBROW, field);
    case MII_FIELD_MOUTH_TYPE:    return part(MII3D_ICON_MOUTH, field);
    case MII_FIELD_MUSTACHE_TYPE: return part(MII3D_ICON_MUSTACHE, field);
    case MII_FIELD_GLASS_TYPE:    return part(MII3D_ICON_GLASSES, field);
    default:                      return nullptr;
    }
}


// ---- page building blocks ----------------------------------------------

using Fill = std::function<void(brls::Box*)>;

// A page body: centered content; `scroll` for grids taller than the screen.
static brls::View* page(Fill fill, bool scroll)
{
    auto* box = new brls::Box(brls::Axis::COLUMN);
    box->setAlignItems(brls::AlignItems::CENTER);
    if (!scroll) {
        box->setJustifyContent(brls::JustifyContent::CENTER);
        box->setPadding(10, 30, 30, 30);
        fill(box);
        return box;
    }
    box->setPadding(24, 30, 40, 30);
    fill(box);
    auto* frame = new brls::ScrollingFrame();
    // follow the focus (the grids route up / down themselves)
    frame->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
    frame->setContentView(box);
    return frame;
}

static OptionGrid* partsGrid(EditorSession* s, mii_field_t field, mii_grid_kind_t kind)
{
    int cols;
    std::vector<uint8_t> items = gridItems(kind, &cols);
    OptionGrid::Options o;
    o.columns = cols;
    o.iconOf = iconFor(field);
    if (field == MII_FIELD_EYE_TYPE || field == MII_FIELD_EYEBROW_TYPE)
        o.iconScale = 0.86f;
    if (field == MII_FIELD_GLASS_TYPE) {
        o.noneIcon = true;
        o.iconScale = 0.8f;
    }
    return new OptionGrid(&s->mii, field, items, o, [s] { s->dirty = true; });
}

static OptionGrid* rangeGrid(EditorSession* s, mii_field_t field, int cols)
{
    OptionGrid::Options o;
    o.columns = cols;
    o.iconOf = iconFor(field);
    return new OptionGrid(&s->mii, field, OptionGrid::range(field), o, [s] { s->dirty = true; });
}

static OptionGrid* colorGrid(EditorSession* s, mii_field_t field)
{
    int cols;
    std::vector<uint8_t> items = gridItems(MII_GRID_COMMON_COLOR, &cols);
    OptionGrid::Options o;
    o.columns = cols;
    o.cell = 56;
    o.gap = 8;
    o.colorOf = [](uint8_t v) { return mii_common_color_rgb(v); };
    return new OptionGrid(&s->mii, field, items, o, [s] { s->dirty = true; });
}

static brls::View* colorPage(EditorSession* s, mii_field_t field)
{
    return page([=](brls::Box* box) { box->addView(colorGrid(s, field)); }, true);
}

struct SliderSpec {
    mii_field_t field;
    const char* key;
};

static brls::View* positionPage(EditorSession* s, std::initializer_list<SliderSpec> sliders)
{
    std::vector<SliderSpec> list(sliders);
    return page([=](brls::Box* box) {
        for (const SliderSpec& sp : list) {
            auto* slider = new FieldSlider(&s->mii, sp.field, brls::getStr(sp.key), [s] { s->dirty = true; });
            slider->setWidth(600);
            box->addView(slider);
        }
    }, false);
}

// ---- category pages ----------------------------------------------------

CategoryActivity::CategoryActivity(std::shared_ptr<EditorSession> session, Category category)
    : session(std::move(session))
    , category(category)
{
}

void CategoryActivity::addTabs()
{
    EditorSession* s = session.get();
    auto types = "editor/tabs/types"_i18n;
    auto position = "editor/tabs/position"_i18n;
    auto color = "editor/tabs/color"_i18n;

    switch (category) {
    case Category::FACE:
        tabs.push_back({ "editor/tabs/shape"_i18n, [s] {
            return page([s](brls::Box* b) { b->addView(rangeGrid(s, MII_FIELD_FACELINE_TYPE, 4)); }, false);
        } });
        tabs.push_back({ "editor/tabs/wrinkles"_i18n, [s] {
            return page([s](brls::Box* b) { b->addView(rangeGrid(s, MII_FIELD_FACELINE_WRINKLE, 6)); }, false);
        } });
        tabs.push_back({ "editor/tabs/makeup"_i18n, [s] {
            return page([s](brls::Box* b) { b->addView(rangeGrid(s, MII_FIELD_FACELINE_MAKE, 6)); }, false);
        } });
        tabs.push_back({ color, [s] {
            return page([s](brls::Box* b) {
                int cols;
                std::vector<uint8_t> items = gridItems(MII_GRID_FACELINE_COLOR, &cols);
                OptionGrid::Options o;
                o.columns = cols;
                o.cell = 80;
                o.colorOf = [](uint8_t v) { return mii_faceline_color_rgb(v); };
                b->addView(new OptionGrid(&s->mii, MII_FIELD_FACELINE_COLOR, items, o, [s] { s->dirty = true; }));
            }, false);
        } });
        break;

    case Category::HAIR:
        tabs.push_back({ types, [s] {
            return page([s](brls::Box* b) { b->addView(partsGrid(s, MII_FIELD_HAIR_TYPE, MII_GRID_HAIR)); }, true);
        } });
        tabs.push_back({ color, [s] {
            return page([s](brls::Box* b) {
                b->addView(colorGrid(s, MII_FIELD_HAIR_COLOR));
            }, true);
        } });
        break;

    case Category::EYEBROWS:
        tabs.push_back({ types, [s] {
            return page([s](brls::Box* b) { b->addView(partsGrid(s, MII_FIELD_EYEBROW_TYPE, MII_GRID_EYEBROW)); }, true);
        } });
        tabs.push_back({ position, [s] {
            return positionPage(s, { { MII_FIELD_EYEBROW_SCALE, "editor/size" },
                                       { MII_FIELD_EYEBROW_ASPECT, "editor/thickness" },
                                       { MII_FIELD_EYEBROW_ROTATE, "editor/rotation" },
                                       { MII_FIELD_EYEBROW_X, "editor/spacing" },
                                       { MII_FIELD_EYEBROW_Y, "editor/height" } });
        } });
        tabs.push_back({ color, [s] { return colorPage(s, MII_FIELD_EYEBROW_COLOR); } });
        break;

    case Category::EYES:
        tabs.push_back({ types, [s] {
            return page([s](brls::Box* b) { b->addView(partsGrid(s, MII_FIELD_EYE_TYPE, MII_GRID_EYE)); }, true);
        } });
        tabs.push_back({ position, [s] {
            return positionPage(s, { { MII_FIELD_EYE_SCALE, "editor/size" },
                                       { MII_FIELD_EYE_ASPECT, "editor/squash" },
                                       { MII_FIELD_EYE_ROTATE, "editor/rotation" },
                                       { MII_FIELD_EYE_X, "editor/spacing" },
                                       { MII_FIELD_EYE_Y, "editor/height" } });
        } });
        tabs.push_back({ color, [s] { return colorPage(s, MII_FIELD_EYE_COLOR); } });
        break;

    case Category::NOSE:
        tabs.push_back({ types, [s] {
            return page([s](brls::Box* b) { b->addView(partsGrid(s, MII_FIELD_NOSE_TYPE, MII_GRID_NOSE)); }, false);
        } });
        tabs.push_back({ position, [s] {
            return positionPage(s, { { MII_FIELD_NOSE_SCALE, "editor/size" }, { MII_FIELD_NOSE_Y, "editor/height" } });
        } });
        break;

    case Category::MOUTH:
        tabs.push_back({ types, [s] {
            return page([s](brls::Box* b) { b->addView(partsGrid(s, MII_FIELD_MOUTH_TYPE, MII_GRID_MOUTH)); }, true);
        } });
        tabs.push_back({ position, [s] {
            return positionPage(s, { { MII_FIELD_MOUTH_SCALE, "editor/size" },
                                       { MII_FIELD_MOUTH_ASPECT, "editor/thickness" },
                                       { MII_FIELD_MOUTH_Y, "editor/height" } });
        } });
        tabs.push_back({ color, [s] { return colorPage(s, MII_FIELD_MOUTH_COLOR); } });
        break;

    case Category::FACIAL_HAIR:
        tabs.push_back({ types, [s] {
            return page([s](brls::Box* b) {
                b->addView(makeSectionTitle("editor/mustache"_i18n));
                OptionGrid::Options o;
                o.columns = 3;
                o.iconOf = iconFor(MII_FIELD_MUSTACHE_TYPE);
                o.noneIcon = true;
                o.iconScale = 0.6f;
                b->addView(new OptionGrid(&s->mii, MII_FIELD_MUSTACHE_TYPE,
                    OptionGrid::range(MII_FIELD_MUSTACHE_TYPE), o, [s] { s->dirty = true; }));
                auto* beard = makeSectionTitle("editor/beard"_i18n);
                beard->setMarginTop(26);
                b->addView(beard);
                b->addView(rangeGrid(s, MII_FIELD_BEARD_TYPE, 3));
            }, true);
        } });
        tabs.push_back({ position, [s] {
            return positionPage(s, { { MII_FIELD_MUSTACHE_SCALE, "editor/size" },
                                       { MII_FIELD_MUSTACHE_Y, "editor/height" } });
        } });
        tabs.push_back({ color, [s] { return colorPage(s, MII_FIELD_BEARD_COLOR); } });
        break;

    case Category::MOLE:
        tabs.push_back({ types, [s] {
            return page([s](brls::Box* b) {
                OptionGrid::Options o;
                o.columns = 2;
                o.iconOf = iconFor(MII_FIELD_MOLE_TYPE);
                b->addView(new OptionGrid(&s->mii, MII_FIELD_MOLE_TYPE, { 0, 1 }, o, [s] { s->dirty = true; }));
            }, false);
        } });
        tabs.push_back({ position, [s] {
            return positionPage(s, { { MII_FIELD_MOLE_SCALE, "editor/size" },
                                       { MII_FIELD_MOLE_X, "editor/horizontal" },
                                       { MII_FIELD_MOLE_Y, "editor/vertical" } });
        } });
        break;

    case Category::GLASSES:
        tabs.push_back({ types, [s] {
            return page([s](brls::Box* b) { b->addView(partsGrid(s, MII_FIELD_GLASS_TYPE, MII_GRID_GLASS)); }, false);
        } });
        tabs.push_back({ position, [s] {
            return positionPage(s, { { MII_FIELD_GLASS_SCALE, "editor/size" }, { MII_FIELD_GLASS_Y, "editor/height" } });
        } });
        tabs.push_back({ color, [s] { return colorPage(s, MII_FIELD_GLASS_COLOR); } });
        break;

    case Category::STYLE:
        tabs.push_back({ "", [s] {
            return page([s](brls::Box* b) {
                b->addView(makePrompt("editor/prompt/style"_i18n));
                OptionGrid::Options o;
                o.columns = 2;
                o.cell = 200;
                o.gap = 16;
                o.paint = [](NVGcontext* vg, uint8_t v, float x, float y, float sz) {
                    drawIcon(vg, v ? "woman" : "man", x + sz * 0.1f, y + sz * 0.1f, sz * 0.8f, ui::kText);
                };
                b->addView(new OptionGrid(&s->mii, MII_FIELD_GENDER, { 0, 1 }, o, [s] { s->dirty = true; }));
                auto* note = new brls::Label();
                note->setText("editor/prompt/style_note"_i18n);
                note->setFontSize(20);
                note->setMarginTop(28);
                b->addView(note);
            }, false);
        } });
        break;

    case Category::BODY:
        tabs.push_back({ "", [s] {
            return page([s](brls::Box* b) {
                b->addView(makePrompt("editor/prompt/body"_i18n));
                auto icon = [](const char* name) { return new IconView(name, 56, nvgRGB(110, 110, 110)); };
                auto changed = [s] { s->dirty = true; };
                b->addView(makeSectionTitle("editor/height"_i18n));
                auto* height = new FieldSlider(&s->mii, MII_FIELD_HEIGHT, icon("shorter"), icon("taller"), changed);
                height->setWidth(600);
                b->addView(height);
                b->addView(makeSectionTitle("editor/build"_i18n));
                auto* build = new FieldSlider(&s->mii, MII_FIELD_BUILD, icon("thinner"), icon("wider"), changed);
                build->setWidth(600);
                b->addView(build);
            }, false);
        } });
        break;

    case Category::FAVORITE_COLOR:
        tabs.push_back({ "", [s] {
            return page([s](brls::Box* b) {
                b->addView(makePrompt("editor/prompt/favorite_color"_i18n));
                OptionGrid::Options o;
                o.columns = 6;
                o.colorOf = [](uint8_t v) { return mii_favorite_color_rgb(v); };
                b->addView(new OptionGrid(&s->mii, MII_FIELD_FAVORITE_COLOR, OptionGrid::range(MII_FIELD_FAVORITE_COLOR),
                    o, [s] { s->dirty = true; }));
            }, false);
        } });
        break;

    case Category::COUNT:
        break;
    }
}

brls::View* CategoryActivity::createContentView()
{
    addTabs();

    auto* right = new brls::Box(brls::Axis::COLUMN);
    if (tabs.size() > 1) {
        std::vector<std::string> names;
        for (const Tab& t : tabs)
            names.push_back(t.name);
        tabBar = new TabBar(names, [this](int i) { setTab(i); });
        tabBar->setMarginTop(8);
        right->addView(tabBar);
    }
    body = new brls::Box(brls::Axis::COLUMN);
    body->setGrow(1);
    right->addView(body);

    // the Style and Body pages show the whole Mii
    const bool whole = category == Category::STYLE || category == Category::BODY;
    brls::AppletFrame* frame = editorFrame(*session, right, nullptr,
        whole ? MiiPreview::Framing::FULL_BODY : MiiPreview::Framing::BUST);
    frame->registerAction("hints/back"_i18n, brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity(brls::TransitionAnimation::NONE);
        return true;
    });
    frame->registerAction("", brls::BUTTON_BACK, [](brls::View*) {
        MiiPreview::showTimings = !MiiPreview::showTimings;
        return true;
    }, true);
    if (category == Category::HAIR) {
        // Square flips the hair's part on any hair tab
        std::shared_ptr<EditorSession> s = session;
        frame->registerAction("editor/flip_part"_i18n, brls::BUTTON_X, [s](brls::View*) {
            s->mii.hair_flip = !s->mii.hair_flip;
            s->dirty = true;
            return true;
        });
    }
    if (tabs.size() > 1) {
        // the tab bar shows the L / R glyphs, so no hints for these
        frame->registerAction("", brls::BUTTON_LB, [this](brls::View*) {
            if (active > 0)
                setTab(active - 1);
            return true;
        }, true);
        frame->registerAction("", brls::BUTTON_RB, [this](brls::View*) {
            if (active + 1 < (int)tabs.size())
                setTab(active + 1);
            return true;
        }, true);
        // desktop builds: Q / E (keyboards have no shoulder buttons)
        frame->registerAction(brls::BrlsKeyCombination(brls::BRLS_KBD_KEY_Q), [this](brls::View*) {
            setTab(active - 1);
            return true;
        });
        frame->registerAction(brls::BrlsKeyCombination(brls::BRLS_KBD_KEY_E), [this](brls::View*) {
            setTab(active + 1);
            return true;
        });
    }
    setTab(0);
    return frame;
}

void CategoryActivity::setTab(int index)
{
    if (index == active || index < 0 || index >= (int)tabs.size())
        return;
    brls::View* next = tabs[index].build();
    next->setGrow(1);
    body->addView(next);
    brls::View* old = current;
    current = next;
    active = index;
    if (tabBar)
        tabBar->setActive(index);
    if (old) {
        // Move focus off the old page before freeing it. If the new page
        // cannot take focus yet, focus the page box itself, so focus never
        // stays on a view that is about to be freed.
        brls::Application::giveFocus(next);
        if (isInside(brls::Application::getCurrentFocus(), old)) {
            next->setFocusable(true);
            brls::Application::giveFocus(next);
        }
        old->setVisibility(brls::Visibility::GONE);
        body->removeView(old);
    }
}

bool CategoryActivity::isInside(brls::View* view, brls::View* ancestor)
{
    for (; view; view = view->hasParent() ? view->getParent() : nullptr)
        if (view == ancestor)
            return true;
    return false;
}

