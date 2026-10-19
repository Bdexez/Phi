#include <bdex/ui/Ui.hpp>

#include <gtest/gtest.h>

using namespace bdex;

namespace {

/// One frame of a panel at (10, 10), 200 wide: a button, a checkbox and a
/// slider, in rows 20 high with 6 of padding after a 20-high title.
struct Panel {
    ui::Context context;
    render::DebugDraw draw;
    bool clicked = false;
    bool toggled = false;
    bool slid = false;
    bool flag = false;
    f32 value = 0.0f;

    void frame(const ui::Input& input) {
        draw.clear();
        context.begin(input, draw);
        context.panel("Tools", Vec2(10.0f), 200.0f);
        clicked = context.button("go");
        toggled = context.checkbox("flag", flag);
        slid = context.slider("amount", value, 0.0f, 10.0f);
        context.endPanel();
        context.end();
    }
};

// Rows: title 10-30, button 36-56, checkbox 62-82, slider 88-108; widgets
// span x 16-204.
constexpr f32 kButtonY = 46.0f;
constexpr f32 kCheckboxY = 72.0f;
constexpr f32 kSliderY = 98.0f;

ui::Input at(f32 x, f32 y, bool down = false, bool pressed = false, bool released = false) {
    return {Vec2(x, y), down, pressed, released};
}

}  // namespace

TEST(Ui, ButtonsClickOnReleaseOverThem) {
    Panel panel;
    panel.frame(at(100, kButtonY));
    panel.frame(at(100, kButtonY, true, true));
    EXPECT_FALSE(panel.clicked);  // not yet: on release
    panel.frame(at(100, kButtonY, false, false, true));
    EXPECT_TRUE(panel.clicked);
    panel.frame(at(100, kButtonY));
    EXPECT_FALSE(panel.clicked);  // once

    // Pressed on it, released elsewhere: no click.
    panel.frame(at(100, kButtonY, true, true));
    panel.frame(at(300, kButtonY, false, false, true));
    EXPECT_FALSE(panel.clicked);
}

TEST(Ui, CheckboxesToggleAndSlidersFollowTheDrag) {
    Panel panel;
    panel.frame(at(30, kCheckboxY, true, true));
    panel.frame(at(30, kCheckboxY, false, false, true));
    EXPECT_TRUE(panel.toggled);
    EXPECT_TRUE(panel.flag);
    panel.frame(at(30, kCheckboxY, true, true));
    panel.frame(at(30, kCheckboxY, false, false, true));
    EXPECT_FALSE(panel.flag);

    // Press at a quarter of the track (x 16-204): 2.5; drag past its end,
    // even off the panel: clamped to 10.
    panel.frame(at(16.0f + 188.0f * 0.25f, kSliderY, true, true));
    EXPECT_TRUE(panel.slid);
    EXPECT_NEAR(panel.value, 2.5f, 1e-4f);
    panel.frame(at(400, 300, true));
    EXPECT_FLOAT_EQ(panel.value, 10.0f);
    EXPECT_TRUE(panel.context.wantsPointer());  // still dragging, off the panel
    panel.frame(at(400, 300, false, false, true));
    panel.frame(at(400, 300));
    EXPECT_FALSE(panel.context.wantsPointer());
    EXPECT_FLOAT_EQ(panel.value, 10.0f);  // let go: it stays

    // Hovering a widget's row of another panel does nothing to these.
    panel.frame(at(100, kSliderY));
    EXPECT_FALSE(panel.slid);
    EXPECT_TRUE(panel.context.wantsPointer());  // over the panel
}

TEST(Ui, PanelsDrawTheirBackgroundUnderTheirWidgets) {
    Panel panel;
    panel.frame(at(-1, -1));
    // First frame: the height is not known yet.
    const usize first = panel.draw.overlayTriangles().size();
    panel.frame(at(-1, -1));
    ASSERT_EQ(panel.draw.overlayTriangles().size(), first + 6);  // plus the background
    // The background is the first rectangle, spanning the whole panel.
    const auto& background = panel.draw.overlayTriangles();
    EXPECT_EQ(background[0].position, Vec3(10.0f, 10.0f, 0.0f));
    EXPECT_EQ(background[2].position, Vec3(210.0f, 10.0f + 20.0f + 6.0f + 3.0f * 26.0f, 0.0f));
    EXPECT_FALSE(panel.draw.overlayVertices().empty());  // text
}
