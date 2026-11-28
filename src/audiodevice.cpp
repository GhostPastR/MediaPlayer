#include "audiodevice.h"

#include "thread"
#include <QDebug>

AudioDevice::AudioDevice(int sampleRate)
{
    initAudioDevice(sampleRate);
}

AudioDevice::~AudioDevice()
{
    uninitAudioDevice();
}

float AudioDevice::volume()
{
    float vol = 1.0f; // значение по умолчанию, если не удалось получить
    ma_device_get_master_volume(&device_, &vol);
    return vol;
}

void AudioDevice::setVolume(float volume)
{
    ma_device_set_master_volume(&device_, volume);
}

void AudioDevice::setPause(bool paused)
{
    paused_ = paused;
}

void AudioDevice::readData(const QByteArray &pcm, int actual)
{
    ma_uint32 framesToWrite = actual / ma_get_bytes_per_frame(ma_format_s16, 2);
    ma_uint32 framesWritten = 0;
    while (framesWritten < framesToWrite) {
        void* pMapped;
        ma_uint32 frames = framesToWrite - framesWritten;
        if (ma_pcm_rb_acquire_write(&rb_, &frames, &pMapped) != MA_SUCCESS || frames == 0) {
            // Буфер полон — притормозим, чтобы dataCallback успел прочитать
            std::this_thread::yield();
            continue;
        }
        memcpy(pMapped, pcm.constData() + framesWritten * 4, frames * 4);
        ma_pcm_rb_commit_write(&rb_, frames);
        framesWritten += frames;
    }
}

bool AudioDevice::notEmptyPcmRb()
{
    return (ma_pcm_rb_pointer_distance(&rb_) > 0);
}

void AudioDevice::clearPcmRb()
{
    if (rbInitialized_) {
        ma_uint32 available = ma_pcm_rb_available_read(&rb_);
        while (available > 0) {
            void* pRead;
            ma_uint32 frames = available;
            if (ma_pcm_rb_acquire_read(&rb_, &frames, &pRead) != MA_SUCCESS || frames == 0){
                break;
            }
            ma_pcm_rb_commit_read(&rb_, frames);
            available -= frames;
        }
    }
}

ma_uint32 AudioDevice::bufferedFrames()
{
    if (!rbInitialized_){
        return 0;
    }
    return ma_pcm_rb_pointer_distance(&rb_);
}

void AudioDevice::initAudioDevice(int sampleRate)
{
    if (ma_pcm_rb_init(ma_format_s16, 2, sampleRate * 2, nullptr, nullptr, &rb_) != MA_SUCCESS) {
        qDebug() << "rb init failed";
        return;
    }
    rbInitialized_ = true;

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_s16;
    config.playback.channels = 2;
    config.sampleRate = sampleRate;
    config.dataCallback = AudioDevice::dataCallback;
    config.pUserData = this;

    auto r = ma_device_init(nullptr, &config, &device_);
    if (r != MA_SUCCESS) {
        qDebug() << "[AudioDecoder] ma_device_init FAILED, code:" << r;
        return;
    }
    qDebug() << "[AudioDecoder] ma_device_init OK";

    r = ma_device_start(&device_);
    if (r != MA_SUCCESS) {
        qDebug() << "[AudioDecoder] ma_device_start FAILED, code:" << r;
        ma_device_uninit(&device_);
        return;
    }
    qDebug() << "[AudioDecoder] ma_device_start OK";
    deviceInitialized_ = true;
}

void AudioDevice::uninitAudioDevice()
{
    if (deviceInitialized_) {
        ma_device_stop(&device_);   // останавливаем callback
        ma_device_uninit(&device_); // освобождаем устройство
        deviceInitialized_ = false;
    }
    if (rbInitialized_) {
        ma_pcm_rb_uninit(&rb_);
        rbInitialized_ = false;
    }
}

void AudioDevice::dataCallback(ma_device* pDevice, void* pOutput,
                               const void* pInput, ma_uint32 frameCount)
{
    Q_UNUSED(pInput);
    AudioDevice* self = static_cast<AudioDevice*>(pDevice->pUserData);
    if (!self || self->paused_) {
        memset(pOutput, 0, frameCount * 4);
        return;
    }

    ma_uint32 framesRead = 0;
    while (framesRead < frameCount) {
        void* pRead;
        ma_uint32 frames = frameCount - framesRead;
        if (ma_pcm_rb_acquire_read(&self->rb_, &frames, &pRead) != MA_SUCCESS || frames == 0) {
            break; // данных нет — добиваем тишиной
        }
        memcpy((char*)pOutput + framesRead * 4, pRead, frames * 4);
        ma_pcm_rb_commit_read(&self->rb_, frames);
        framesRead += frames;
    }

    if (framesRead < frameCount) {
        memset((char*)pOutput + framesRead * 4, 0, (frameCount - framesRead) * 4);
    }

}
