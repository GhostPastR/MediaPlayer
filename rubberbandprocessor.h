#ifndef RUBBERBANDPROCESSOR_H
#define RUBBERBANDPROCESSOR_H

#include <memory>
#include <RubberBandStretcher.h>

class RubberBandProcessor
{
public:
    RubberBandProcessor(int sampleRate, int channels);
    ~RubberBandProcessor();

    RubberBandProcessor(RubberBandProcessor&&) = default;
    RubberBandProcessor& operator=(RubberBandProcessor&&) = default;
    RubberBandProcessor(const RubberBandProcessor&) = delete;
    RubberBandProcessor& operator=(const RubberBandProcessor&) = delete;

    void setSpeed(double speed);
    void process(const float* const* input, size_t numSamples);
    int available() const;
    size_t retrieve(float* const* output, size_t numSamples);
    int getStartDelay() const;

    void reset(int sampleRate, int channels);

private:
    std::unique_ptr<RubberBand::RubberBandStretcher> stretcher_;
};

#endif // RUBBERBANDPROCESSOR_H
