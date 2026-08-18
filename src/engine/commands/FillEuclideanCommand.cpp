#include "FillEuclideanCommand.h"

#include "../AudioEngine.h"
#include "../EuclideanRhythm.h"
#include "../MidiConstants.h"
#include "../SamplerInstrument.h"

FillEuclideanCommand::FillEuclideanCommand(AudioEngine& engine,
                                            int patternIndex,
                                            int padIndex,
                                            int pulses,
                                            int rotation,
                                            int velocity)
    : engine_(engine),
      patternIndex_(patternIndex),
      padIndex_(padIndex),
      pulses_(pulses),
      rotation_(rotation),
      velocity_(juce::jlimit(midi::kVelocityLiveMin, midi::kVelocityMax, velocity))
{
    // Capture pre-change state now so perform()/undo()/perform() cycles don't
    // clobber the snapshot by re-reading the already-applied state.
    auto& sampler = engine_.getSampler();
    const int steps = sampler.getStepCount();
    if (steps <= 0 || steps > kMaxSteps) return;

    priorStepCount_ = steps;
    priorSteps_.fill(false);
    priorVelocities_.fill(100);
    const auto snapshot = sampler.getPadsSnapshot(
        SamplerInstrument::SnapshotContent::Patterns);
    for (const auto& pad : snapshot)
    {
        if (pad.id != padIndex_) continue;
        for (const auto& ps : pad.patterns)
        {
            if (ps.patternIndex != patternIndex_) continue;
            for (int i = 0; i < steps; ++i)
                priorSteps_[(size_t) i] = ps.steps[i];
            for (int i = 0; i < steps && i < (int) ps.velocities.size(); ++i)
                priorVelocities_[(size_t) i] = ps.velocities[(size_t) i];
            return;
        }
    }
}

bool FillEuclideanCommand::perform()
{
    auto& sampler = engine_.getSampler();
    const int steps = sampler.getStepCount();
    if (steps <= 0 || steps > kMaxSteps) return false;

    const auto euclid = EuclideanRhythm::pattern(pulses_, steps, rotation_);
    for (int i = 0; i < steps; ++i)
    {
        const bool hit = (int) i < (int) euclid.size() && euclid[(size_t) i];
        sampler.setStep(patternIndex_, padIndex_, i, hit);
        if (hit)
            sampler.setStepVelocity(patternIndex_, padIndex_, i, velocity_);
    }
    return true;
}

bool FillEuclideanCommand::undo()
{
    auto& sampler = engine_.getSampler();
    for (int i = 0; i < priorStepCount_; ++i)
    {
        sampler.setStep(patternIndex_, padIndex_, i, priorSteps_[(size_t) i]);
        if (priorSteps_[(size_t) i])
            sampler.setStepVelocity(patternIndex_, padIndex_, i,
                                    priorVelocities_[(size_t) i]);
    }
    return true;
}
