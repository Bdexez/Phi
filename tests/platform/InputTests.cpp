#include <bdex/core/EventBus.hpp>
#include <bdex/platform/Events.hpp>
#include <bdex/platform/Input.hpp>

#include <gtest/gtest.h>

using namespace bdex;
using namespace bdex::platform;

TEST(InputState, TracksKeyTransitionsPerFrame) {
    EventBus events;
    InputState input(events);

    input.beginFrame();
    events.publish(KeyEvent{Key::W, KeyAction::Press});
    EXPECT_TRUE(input.keyDown(Key::W));
    EXPECT_TRUE(input.keyPressed(Key::W));
    EXPECT_FALSE(input.keyReleased(Key::W));

    input.beginFrame();
    EXPECT_TRUE(input.keyDown(Key::W));
    EXPECT_FALSE(input.keyPressed(Key::W));

    input.beginFrame();
    events.publish(KeyEvent{Key::W, KeyAction::Release});
    EXPECT_FALSE(input.keyDown(Key::W));
    EXPECT_TRUE(input.keyReleased(Key::W));

    // A tap within one frame is still seen as pressed and released.
    input.beginFrame();
    events.publish(KeyEvent{Key::Space, KeyAction::Press});
    events.publish(KeyEvent{Key::Space, KeyAction::Release});
    EXPECT_TRUE(input.keyPressed(Key::Space));
    EXPECT_TRUE(input.keyReleased(Key::Space));
    EXPECT_FALSE(input.keyDown(Key::Space));
}

TEST(InputState, TracksMouseButtonTransitionsPerFrame) {
    EventBus events;
    InputState input(events);

    input.beginFrame();
    events.publish(MouseButtonEvent{MouseButton::Right, true});
    EXPECT_TRUE(input.mouseButtonDown(MouseButton::Right));
    EXPECT_TRUE(input.mouseButtonPressed(MouseButton::Right));
    EXPECT_FALSE(input.mouseButtonReleased(MouseButton::Right));

    input.beginFrame();
    events.publish(MouseButtonEvent{MouseButton::Right, false});
    EXPECT_FALSE(input.mouseButtonDown(MouseButton::Right));
    EXPECT_FALSE(input.mouseButtonPressed(MouseButton::Right));
    EXPECT_TRUE(input.mouseButtonReleased(MouseButton::Right));

    input.beginFrame();
    EXPECT_FALSE(input.mouseButtonReleased(MouseButton::Right));
}

TEST(InputState, LosingFocusReleasesEverything) {
    EventBus events;
    InputState input(events);

    input.beginFrame();
    events.publish(KeyEvent{Key::A, KeyAction::Press});
    events.publish(MouseButtonEvent{MouseButton::Left, true});
    events.publish(MouseMoved{100.0, 100.0});

    input.beginFrame();
    events.publish(WindowFocusChanged{nullptr, false});
    EXPECT_FALSE(input.keyDown(Key::A));
    EXPECT_TRUE(input.keyReleased(Key::A));
    EXPECT_FALSE(input.mouseButtonDown(MouseButton::Left));
    EXPECT_TRUE(input.mouseButtonReleased(MouseButton::Left));
    EXPECT_FALSE(input.keyReleased(Key::B));  // was not held

    // Back in focus, the cursor re-enters elsewhere without a delta spike.
    input.beginFrame();
    events.publish(WindowFocusChanged{nullptr, true});
    events.publish(MouseMoved{900.0, 20.0});
    EXPECT_EQ(input.mouseDelta(), Vec2(0.0f));
    events.publish(MouseMoved{905.0, 20.0});
    EXPECT_EQ(input.mouseDelta(), Vec2(5.0f, 0.0f));
}
