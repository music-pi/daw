#pragma once

#include "InputManager.h"

/**
 * RAII wrapper for InputManager handler IDs.
 *
 * Automatically removes the handler from InputManager when destroyed,
 * preventing handler leaks and use-after-free issues.
 *
 * Example usage:
 *
 *   ScopedInputHandler handler(
 *       &inputManager,
 *       inputManager.addPadHandler(
 *           InputManager::HandlerPriority::View,
 *           "audio_editor",
 *           [](InputEvent& e) { handleEvent(e); }
 *       )
 *   );
 *   // Handler automatically removed when 'handler' goes out of scope
 */
class ScopedInputHandler
{
public:
    /**
     * Default constructor creates an invalid handler.
     */
    ScopedInputHandler() = default;

    /**
     * Construct a scoped handler.
     *
     * @param manager Pointer to InputManager (may be nullptr)
     * @param id Handler ID returned from InputManager::addHandler()
     */
    ScopedInputHandler(InputManager* manager, InputManager::BindingId id)
        : manager_(manager), id_(id)
    {
    }

    /**
     * Destructor removes the handler if valid.
     */
    ~ScopedInputHandler()
    {
        if (id_ != 0 && manager_ != nullptr)
            manager_->removeHandler(id_);
    }

    // Non-copyable
    ScopedInputHandler(const ScopedInputHandler&) = delete;
    ScopedInputHandler& operator=(const ScopedInputHandler&) = delete;

    /**
     * Move constructor transfers ownership.
     * Source handler becomes invalid after move.
     */
    ScopedInputHandler(ScopedInputHandler&& other) noexcept
        : manager_(other.manager_), id_(other.id_)
    {
        other.id_ = 0;
    }

    /**
     * Move assignment transfers ownership.
     * Removes current handler (if valid) before taking ownership of new handler.
     */
    ScopedInputHandler& operator=(ScopedInputHandler&& other) noexcept
    {
        if (this != &other)
        {
            // Remove current handler
            if (id_ != 0 && manager_ != nullptr)
                manager_->removeHandler(id_);

            // Transfer ownership
            manager_ = other.manager_;
            id_ = other.id_;
            other.id_ = 0;
        }
        return *this;
    }

    /**
     * Release ownership of the handler without removing it.
     *
     * @return The handler ID (caller is responsible for removing)
     */
    InputManager::BindingId release() noexcept
    {
        auto oldId = id_;
        id_ = 0;
        return oldId;
    }

    /**
     * Get the handler ID.
     *
     * @return Handler ID (0 if invalid)
     */
    InputManager::BindingId get() const noexcept
    {
        return id_;
    }

    /**
     * Check if the handler is valid (registered).
     *
     * @return true if handler ID is non-zero
     */
    bool valid() const noexcept
    {
        return id_ != 0;
    }

private:
    InputManager* manager_ { nullptr };
    InputManager::BindingId id_ { 0 };
};
