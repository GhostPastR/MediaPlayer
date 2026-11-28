#ifndef AUDIODEVICE_H
#define AUDIODEVICE_H

#include <atomic>
#include <memory>
#include <QByteArray>
#include "miniaudio.h"

class MediaClock;

class AudioDevice
{
public:
    AudioDevice(int sampleRate);
    ~AudioDevice();

    float volume();
    void setVolume(float volume);

    void setPause(bool paused);
    void readData(const QByteArray &pcm, int actual);
    bool notEmptyPcmRb();
    void clearPcmRb();
    ma_uint32 bufferedFrames();

private:
    std::atomic<bool> paused_{};
    ma_device device_;
    ma_pcm_rb rb_;
    bool deviceInitialized_{};
    bool rbInitialized_{};

    void initAudioDevice(int sampleRate);
    void uninitAudioDevice();

    static void dataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
};

#endif // AUDIODEVICE_H
