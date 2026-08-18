#include "SetSwingCommand.h"
#include "../AudioEngine.h"

SetSwingCommand::SetSwingCommand(AudioEngine& engine_, double oldPercent_, double newPercent_)
    : engine(engine_)
    , oldPercent(oldPercent_)
    , newPercent(newPercent_)
{
}

bool SetSwingCommand::perform()
{
    engine.setSwingPercent(newPercent);
    return true;
}

bool SetSwingCommand::undo()
{
    engine.setSwingPercent(oldPercent);
    return true;
}

juce::UndoableAction* SetSwingCommand::createCoalescedAction(UndoableAction* nextAction)
{
    if (auto* next = dynamic_cast<SetSwingCommand*>(nextAction))
    {
        if (&next->engine == &engine)
            return new SetSwingCommand(engine, oldPercent, next->newPercent);
    }
    return nullptr;
}
