#ifndef FILTERGRAPH_H
#define FILTERGRAPH_H

#include <memory>

class AVFilterGraph;
class AVFilterContext;
class Frame;
class MediaClock;
class AudioDevice;
class SwrContext;

class FilterGraph
{
public:
    FilterGraph();
    ~FilterGraph();

    bool init(double speed, int rate);   // создание графа atempo
    void free();

    void avBuffersrc(const Frame &fltFrame, const Frame &filteredFrame, double rate, double flush,
                     SwrContext* swrContextOut, std::shared_ptr<MediaClock> mediaClock,
                     std::unique_ptr<AudioDevice> &audioDevice);

private:
    AVFilterGraph *filterGraph_{};
    AVFilterContext *bufferSrcCtx_{};
    AVFilterContext *bufferSinkCtx_{};
    double speed_{1.0};
};

#endif // FILTERGRAPH_H
