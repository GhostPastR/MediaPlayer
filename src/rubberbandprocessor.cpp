#include "rubberbandprocessor.h"

RubberBandProcessor::RubberBandProcessor(int sampleRate, int channels)
{
    int options = RubberBand::RubberBandStretcher::OptionProcessRealTime;
    stretcher_ = std::make_unique<RubberBand::RubberBandStretcher>(
        sampleRate, channels, options, 1.0, 1.0);
}

RubberBandProcessor::~RubberBandProcessor()
{

}

void RubberBandProcessor::setSpeed(double speed)
{
    stretcher_->setTimeRatio(1.0 / speed);
}

void RubberBandProcessor::process(const float* const* input, size_t numSamples)
{
    stretcher_->process(input, numSamples, false);
}

int RubberBandProcessor::available() const
{
    return stretcher_->available();
}

size_t RubberBandProcessor::retrieve(float* const* output, size_t numSamples)
{
    return stretcher_->retrieve(output, numSamples);
}

int RubberBandProcessor::getStartDelay() const
{
    return stretcher_->getStartDelay();
}

void RubberBandProcessor::reset(int sampleRate, int channels)
{
    int options = RubberBand::RubberBandStretcher::OptionProcessRealTime
                | RubberBand::RubberBandStretcher::OptionEngineFiner
                | RubberBand::RubberBandStretcher::OptionFormantPreserved
                | RubberBand::RubberBandStretcher::OptionWindowShort;
    stretcher_ = std::make_unique<RubberBand::RubberBandStretcher>(
        sampleRate, channels, options, 1.0, 1.0);
}
