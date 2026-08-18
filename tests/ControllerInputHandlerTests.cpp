#include <gtest/gtest.h>

#include "../src/input/ControllerInputHandler.h"
#include "../src/input/InputManager.h"

namespace
{

class ControllerInputHandlerTest : public ::testing::Test
{
protected:
    ControllerInputHandlerTest()
        : handler(inputManager)
    {
    }

    InputManager inputManager;
    ControllerInputHandler handler;
};

TEST_F(ControllerInputHandlerTest, EmitsControlEventsWithDefaultSource)
{
    bool triggered = false;
    inputManager.addControllerHandler(InputManager::HandlerPriority::Global, "",
                                      "transport.play", [&](InputEvent& event)
    {
        triggered = true;
        EXPECT_EQ(event.source, "controller");
        EXPECT_EQ(event.metadata.getWithDefault("origin", {}).toString(), "ui");
        EXPECT_TRUE(event.metadata.getWithDefault("device", {}).toString().isEmpty());
    });

    juce::NamedValueSet payload;
    payload.set("origin", "ui");

    EXPECT_TRUE(handler.emitControl("transport.play", payload));
    EXPECT_TRUE(triggered);
}

TEST_F(ControllerInputHandlerTest, HonorsExplicitSource)
{
    bool triggered = false;
    inputManager.addControllerHandler(InputManager::HandlerPriority::Global, "",
                                      "transport.stop", [&](InputEvent& event)
    {
        triggered = true;
        EXPECT_EQ(event.source, "mk3");
    });

    EXPECT_TRUE(handler.emitControl("transport.stop", {}, "mk3"));
    EXPECT_TRUE(triggered);
}

TEST_F(ControllerInputHandlerTest, AttachingMk3AddsMetadata)
{
    bool triggered = false;
    inputManager.addControllerHandler(InputManager::HandlerPriority::Global, "",
                                      "transport.record", [&](InputEvent& event)
    {
        triggered = true;
        EXPECT_EQ(event.metadata.getWithDefault("device", {}).toString(), "mk3");
        EXPECT_EQ(event.metadata.getWithDefault("origin", {}).toString(), "pads");
    });

    handler.attachDevice(reinterpret_cast<Mk3Device*>(0x1));

    juce::NamedValueSet payload;
    payload.set("origin", "pads");

    EXPECT_TRUE(handler.emitControl("transport.record", payload));
    EXPECT_TRUE(triggered);

    handler.detachDevice();
}

} // namespace

