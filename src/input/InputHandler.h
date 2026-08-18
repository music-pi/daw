#pragma once

#include <string_view>

class InputManager;

class InputHandler
{
public:
    explicit InputHandler(InputManager& mgr) : inputManager(mgr) {}
    virtual ~InputHandler() = default;

    virtual void start() {}
    virtual void stop() {}
    virtual std::string_view name() const = 0;

protected:
    InputManager& inputManager;
};
