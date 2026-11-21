#include <phi/core/EventBus.hpp>
#include <phi/platform/Actions.hpp>
#include <phi/platform/Events.hpp>
#include <phi/platform/Input.hpp>

#include <gtest/gtest.h>

using namespace phi;
using namespace phi::platform;

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

TEST(KeyNames, RoundTripEveryKey) {
    usize named = 0;
    for (u16 value = 0; value < static_cast<u16>(Key::Count); ++value) {
        const Key key = static_cast<Key>(value);
        const std::string_view name = keyName(key);
        if (name.empty()) {
            continue;
        }
        ++named;
        EXPECT_EQ(parseKey(name), key) << name;
    }
    EXPECT_EQ(named, 83u);  // every enumerator but Unknown and Count
    EXPECT_EQ(keyName(Key::Unknown), "");
}

TEST(KeyNames, ParsingIgnoresCaseAndAcceptsAliases) {
    EXPECT_EQ(parseKey("leftshift"), Key::LeftShift);
    EXPECT_EQ(parseKey("SPACE"), Key::Space);
    EXPECT_EQ(parseKey("ctrl"), Key::LeftControl);
    EXPECT_EQ(parseKey("Num7"), Key::Num7);
    EXPECT_EQ(parseKey("7"), Key::Num7);
    EXPECT_EQ(parseKey("Esc"), Key::Escape);
    EXPECT_FALSE(parseKey("NotAKey").has_value());
    EXPECT_FALSE(parseKey("").has_value());

    EXPECT_EQ(parseMouseButton("mouseright"), MouseButton::Right);
    EXPECT_EQ(mouseButtonName(MouseButton::Middle), "MouseMiddle");
    EXPECT_FALSE(parseMouseButton("Mouse9").has_value());
}

TEST(InputActions, BindsKeysAndButtonsFromConfiguration) {
    EventBus events;
    InputState input(events);
    InputActions actions(input);

    Config config;
    config.set("input.jump", "Space");
    config.set("input.fire", "MouseLeft, f");
    config.set("input.broken", "Space, Kaboom");
    config.set("window.width", "800");  // other sections are ignored
    const auto loaded = actions.load(config);
    ASSERT_FALSE(loaded.has_value());  // "broken" is reported...
    EXPECT_NE(loaded.error().message.find("Kaboom"), std::string::npos);
    EXPECT_TRUE(actions.has("jump"));  // ...the others still bound
    EXPECT_TRUE(actions.has("fire"));
    EXPECT_FALSE(actions.has("broken"));
    EXPECT_FALSE(actions.has("width"));
    EXPECT_EQ(actions.bindings("fire"), (std::vector<std::string>{"MouseLeft", "F"}));

    input.beginFrame();
    events.publish(KeyEvent{Key::F, KeyAction::Press});
    EXPECT_TRUE(actions.down("fire"));
    EXPECT_TRUE(actions.pressed("fire"));
    EXPECT_FALSE(actions.down("jump"));
    EXPECT_FALSE(actions.down("unknown"));

    // Held through two bindings: releasing one keeps the action down.
    input.beginFrame();
    events.publish(MouseButtonEvent{MouseButton::Left, true});
    events.publish(KeyEvent{Key::F, KeyAction::Release});
    EXPECT_TRUE(actions.down("fire"));
    EXPECT_FALSE(actions.released("fire"));

    input.beginFrame();
    events.publish(MouseButtonEvent{MouseButton::Left, false});
    EXPECT_FALSE(actions.down("fire"));
    EXPECT_TRUE(actions.released("fire"));
}

TEST(InputActions, RebindingReplacesBindings) {
    EventBus events;
    InputState input(events);
    InputActions actions(input);
    ASSERT_TRUE(actions.bind("jump", "Space").has_value());
    ASSERT_TRUE(actions.bind("jump", "J, K").has_value());
    EXPECT_EQ(actions.bindings("jump"), (std::vector<std::string>{"J", "K"}));
    EXPECT_FALSE(actions.bind("jump", "Nope").has_value());
    EXPECT_EQ(actions.bindings("jump"), (std::vector<std::string>{"J", "K"}));  // unchanged
}

TEST(InputState, TracksGamepads) {
    EventBus events;
    InputState input(events);
    EXPECT_FALSE(input.gamepadConnected(0));

    input.beginFrame();
    events.publish(GamepadConnected{1, true});
    events.publish(GamepadButtonEvent{1, GamepadButton::A, true});
    events.publish(GamepadAxisMoved{1, GamepadAxis::LeftY, -0.75f});
    EXPECT_TRUE(input.gamepadConnected(1));
    EXPECT_TRUE(input.gamepadButtonDown(GamepadButton::A, 1));
    EXPECT_TRUE(input.gamepadButtonPressed(GamepadButton::A, 1));
    EXPECT_FALSE(input.gamepadButtonDown(GamepadButton::A, 0));  // another pad
    EXPECT_FLOAT_EQ(input.gamepadAxis(GamepadAxis::LeftY, 1), -0.75f);

    input.beginFrame();
    EXPECT_FALSE(input.gamepadButtonPressed(GamepadButton::A, 1));
    EXPECT_TRUE(input.gamepadButtonDown(GamepadButton::A, 1));

    // Unplugging releases everything and recenters the sticks.
    input.beginFrame();
    events.publish(GamepadConnected{1, false});
    EXPECT_FALSE(input.gamepadConnected(1));
    EXPECT_TRUE(input.gamepadButtonReleased(GamepadButton::A, 1));
    EXPECT_FLOAT_EQ(input.gamepadAxis(GamepadAxis::LeftY, 1), 0.0f);

    // Out-of-range pads are ignored.
    events.publish(GamepadButtonEvent{9, GamepadButton::B, true});
    EXPECT_FALSE(input.gamepadButtonDown(GamepadButton::B, 9));
}

TEST(KeyNames, GamepadButtonsHaveNames) {
    EXPECT_EQ(gamepadButtonName(GamepadButton::Start), "GamepadStart");
    EXPECT_EQ(parseGamepadButton("gamepaddpadleft"), GamepadButton::DpadLeft);
    EXPECT_FALSE(parseGamepadButton("GamepadZ").has_value());
}

TEST(InputActions, ReadGamepadButtonsAndSticks) {
    EventBus events;
    InputState input(events);
    InputActions actions(input);
    ASSERT_TRUE(actions.bind("forward", "W, LeftStickUp").has_value());
    ASSERT_TRUE(actions.bind("jump", "Space, GamepadA").has_value());
    ASSERT_TRUE(actions.bind("brake", "LeftTrigger").has_value());
    EXPECT_EQ(actions.bindings("forward"), (std::vector<std::string>{"W", "LeftStickUp"}));

    input.beginFrame();
    events.publish(GamepadConnected{2, true});
    events.publish(GamepadAxisMoved{2, GamepadAxis::LeftY, -0.1f});  // inside the dead zone
    EXPECT_FLOAT_EQ(actions.value("forward"), 0.0f);

    events.publish(GamepadAxisMoved{2, GamepadAxis::LeftY, -0.6f});  // halfway past it
    EXPECT_FLOAT_EQ(actions.value("forward"), 0.5f);
    events.publish(GamepadAxisMoved{2, GamepadAxis::LeftY, -0.4f});
    EXPECT_FLOAT_EQ(actions.value("forward"), 0.25f);
    EXPECT_FALSE(actions.down("forward"));  // not past halfway
    events.publish(GamepadAxisMoved{2, GamepadAxis::LeftY, 0.9f});   // pushed the other way
    EXPECT_FLOAT_EQ(actions.value("forward"), 0.0f);
    events.publish(GamepadAxisMoved{2, GamepadAxis::LeftY, -1.0f});
    EXPECT_FLOAT_EQ(actions.value("forward"), 1.0f);
    EXPECT_TRUE(actions.down("forward"));

    events.publish(GamepadAxisMoved{2, GamepadAxis::LeftTrigger, 0.6f});
    EXPECT_FLOAT_EQ(actions.value("brake"), 0.5f);

    events.publish(GamepadButtonEvent{2, GamepadButton::A, true});
    EXPECT_TRUE(actions.pressed("jump"));
    EXPECT_FLOAT_EQ(actions.value("jump"), 1.0f);

    // A disconnected pad no longer counts.
    input.beginFrame();
    events.publish(GamepadConnected{2, false});
    EXPECT_FLOAT_EQ(actions.value("forward"), 0.0f);
    EXPECT_TRUE(actions.released("jump"));
}
