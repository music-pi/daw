#include <gtest/gtest.h>
#include "input/ScopedInputHandler.h"
#include "input/InputManager.h"

class ScopedInputHandlerTests : public ::testing::Test
{
protected:
    InputManager inputManager;
};

TEST_F(ScopedInputHandlerTests, DefaultConstructor)
{
    ScopedInputHandler handler;
    EXPECT_EQ(handler.get(), 0u);
    EXPECT_FALSE(handler.valid());
}

TEST_F(ScopedInputHandlerTests, DestructorRemovesHandler)
{
    // Add a handler and capture its ID
    InputManager::BindingId handlerId = 0;
    bool handlerCalled = false;

    {
        handlerId = inputManager.addHandler(
            InputManager::HandlerPriority::View,
            "test_context",
            InputEvent::Type::Pad,
            [&handlerCalled](InputEvent& e) { handlerCalled = true; }
        );

        ScopedInputHandler handler(&inputManager, handlerId);
        EXPECT_EQ(handler.get(), handlerId);
        EXPECT_TRUE(handler.valid());

        // Activate context so handler can receive events
        inputManager.setActiveContext("test_context", true);

        // Handler should work before destruction
        EXPECT_TRUE(inputManager.dispatchPad(0, true, 100, false, "test", {}));
        EXPECT_TRUE(handlerCalled);
        handlerCalled = false;

        // ScopedInputHandler goes out of scope here and removes handler
    }

    // Handler should be removed after destruction
    EXPECT_FALSE(inputManager.dispatchPad(0, true, 100, false, "test", {}));
    EXPECT_FALSE(handlerCalled);
}

TEST_F(ScopedInputHandlerTests, MoveConstructorTransfersOwnership)
{
    auto handlerId = inputManager.addHandler(
        InputManager::HandlerPriority::View,
        "test_context",
        InputEvent::Type::Pad,
        [](InputEvent& e) {}
    );

    ScopedInputHandler handler1(&inputManager, handlerId);
    EXPECT_EQ(handler1.get(), handlerId);
    EXPECT_TRUE(handler1.valid());

    // Move construct handler2 from handler1
    ScopedInputHandler handler2(std::move(handler1));

    // handler2 should have ownership
    EXPECT_EQ(handler2.get(), handlerId);
    EXPECT_TRUE(handler2.valid());

    // handler1 should be invalid after move
    EXPECT_EQ(handler1.get(), 0u);
    EXPECT_FALSE(handler1.valid());
}

TEST_F(ScopedInputHandlerTests, MoveAssignmentTransfersOwnership)
{
    bool handler1Called = false;
    bool handler2Called = false;

    auto handlerId1 = inputManager.addHandler(
        InputManager::HandlerPriority::View,
        "test_context",
        InputEvent::Type::Pad,
        [&handler1Called](InputEvent& e) { handler1Called = true; }
    );

    auto handlerId2 = inputManager.addHandler(
        InputManager::HandlerPriority::View,
        "test_context",
        InputEvent::Type::Button,
        [&handler2Called](InputEvent& e) { handler2Called = true; }
    );

    ScopedInputHandler handlerA(&inputManager, handlerId1);
    ScopedInputHandler handlerB(&inputManager, handlerId2);

    inputManager.setActiveContext("test_context", true);

    // Verify both handlers work before move assignment
    EXPECT_TRUE(inputManager.dispatchPad(0, true, 100, false, "test", {}));
    EXPECT_TRUE(handler1Called);
    handler1Called = false;

    EXPECT_TRUE(inputManager.dispatchButton("test_button", true, false, "test", {}));
    EXPECT_TRUE(handler2Called);
    handler2Called = false;

    // Move assign handlerB to handlerA (handlerA's old handler should be removed)
    handlerA = std::move(handlerB);

    // handlerA should now own handlerId2
    EXPECT_EQ(handlerA.get(), handlerId2);
    EXPECT_TRUE(handlerA.valid());

    // handlerB should be invalid
    EXPECT_EQ(handlerB.get(), 0u);
    EXPECT_FALSE(handlerB.valid());

    // Old handler (handlerId1) should be removed
    EXPECT_FALSE(inputManager.dispatchPad(0, true, 100, false, "test", {}));
    EXPECT_FALSE(handler1Called);

    // New handler (handlerId2) should still work
    EXPECT_TRUE(inputManager.dispatchButton("test_button", true, false, "test", {}));
    EXPECT_TRUE(handler2Called);
}

TEST_F(ScopedInputHandlerTests, ReleaseDetachesHandler)
{
    bool handlerCalled = false;

    auto handlerId = inputManager.addHandler(
        InputManager::HandlerPriority::View,
        "test_context",
        InputEvent::Type::Pad,
        [&handlerCalled](InputEvent& e) { handlerCalled = true; }
    );

    inputManager.setActiveContext("test_context", true);

    InputManager::BindingId releasedId;

    {
        ScopedInputHandler handler(&inputManager, handlerId);
        EXPECT_TRUE(handler.valid());

        // Release ownership
        releasedId = handler.release();

        EXPECT_EQ(releasedId, handlerId);
        EXPECT_EQ(handler.get(), 0u);
        EXPECT_FALSE(handler.valid());

        // Handler goes out of scope but should NOT remove the handler
    }

    // Handler should still be registered and work
    EXPECT_TRUE(inputManager.dispatchPad(0, true, 100, false, "test", {}));
    EXPECT_TRUE(handlerCalled);

    // Clean up the released handler manually
    inputManager.removeHandler(releasedId);
}

TEST_F(ScopedInputHandlerTests, NullManagerSafe)
{
    // Default constructed handler with null manager
    {
        ScopedInputHandler handler1;
        EXPECT_FALSE(handler1.valid());
        // Should not crash on destruction
    }

    // Explicitly constructed with nullptr manager
    {
        ScopedInputHandler handler2(nullptr, 123);
        EXPECT_EQ(handler2.get(), 123u);
        EXPECT_TRUE(handler2.valid()); // valid() only checks if id != 0
        // Should not crash on destruction (nullptr check in destructor)
    }

    // Move from null manager handler
    {
        ScopedInputHandler handler3(nullptr, 456);
        ScopedInputHandler handler4(std::move(handler3));
        EXPECT_EQ(handler4.get(), 456u);
        EXPECT_FALSE(handler3.valid());
        // Should not crash on destruction
    }
}

TEST_F(ScopedInputHandlerTests, SelfMoveAssignment)
{
    auto handlerId = inputManager.addHandler(
        InputManager::HandlerPriority::View,
        "test_context",
        InputEvent::Type::Pad,
        [](InputEvent& e) {}
    );

    ScopedInputHandler handler(&inputManager, handlerId);
    EXPECT_TRUE(handler.valid());

    // Self-assignment should be a no-op; cast through ref to avoid -Wself-move
    auto& handlerRef = handler;
    handler = std::move(handlerRef);

    EXPECT_EQ(handler.get(), handlerId);
    EXPECT_TRUE(handler.valid());
}

TEST_F(ScopedInputHandlerTests, MultipleHandlersIndependentLifetimes)
{
    bool handler1Called = false;
    bool handler2Called = false;
    bool handler3Called = false;

    auto id1 = inputManager.addHandler(
        InputManager::HandlerPriority::View,
        "ctx1",
        InputEvent::Type::Pad,
        [&handler1Called](InputEvent& e) { handler1Called = true; }
    );

    auto id2 = inputManager.addHandler(
        InputManager::HandlerPriority::View,
        "ctx2",
        InputEvent::Type::Pad,
        [&handler2Called](InputEvent& e) { handler2Called = true; }
    );

    auto id3 = inputManager.addHandler(
        InputManager::HandlerPriority::View,
        "ctx3",
        InputEvent::Type::Pad,
        [&handler3Called](InputEvent& e) { handler3Called = true; }
    );

    inputManager.setActiveContext("ctx1", true);
    inputManager.setActiveContext("ctx2", true);
    inputManager.setActiveContext("ctx3", true);

    {
        ScopedInputHandler h1(&inputManager, id1);
        {
            ScopedInputHandler h2(&inputManager, id2);
            {
                ScopedInputHandler h3(&inputManager, id3);

                // All three should work
                inputManager.dispatchPad(0, true, 100, false, "test", {});
                EXPECT_TRUE(handler1Called && handler2Called && handler3Called);
                handler1Called = handler2Called = handler3Called = false;

                // h3 goes out of scope, removes handler 3
            }

            // h1 and h2 should still work, h3 should not
            inputManager.dispatchPad(0, true, 100, false, "test", {});
            EXPECT_TRUE(handler1Called);
            EXPECT_TRUE(handler2Called);
            EXPECT_FALSE(handler3Called);
            handler1Called = handler2Called = false;

            // h2 goes out of scope, removes handler 2
        }

        // Only h1 should work
        inputManager.dispatchPad(0, true, 100, false, "test", {});
        EXPECT_TRUE(handler1Called);
        EXPECT_FALSE(handler2Called);
        EXPECT_FALSE(handler3Called);
        handler1Called = false;

        // h1 goes out of scope, removes handler 1
    }

    // None should work
    inputManager.dispatchPad(0, true, 100, false, "test", {});
    EXPECT_FALSE(handler1Called);
    EXPECT_FALSE(handler2Called);
    EXPECT_FALSE(handler3Called);
}
