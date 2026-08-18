#include "SetChokeGroupCommand.h"
#include "../SamplerInstrument.h"

SetChokeGroupCommand::SetChokeGroupCommand(SamplerInstrument& sampler, int padId, int oldGroup, int newGroup)
    : sampler(sampler)
    , padId(padId)
    , oldGroup(oldGroup)
    , newGroup(newGroup)
{
}

bool SetChokeGroupCommand::perform()
{
    sampler.setChokeGroupDirect(padId, newGroup);
    return true;
}

bool SetChokeGroupCommand::undo()
{
    sampler.setChokeGroupDirect(padId, oldGroup);
    return true;
}
