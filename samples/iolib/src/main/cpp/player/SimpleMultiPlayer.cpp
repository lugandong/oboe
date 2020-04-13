/*
 * Copyright 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <android/log.h>

// parselib includes
#include <io/stream/MemInputStream.h>
#include <io/wav/WavStreamReader.h>

// local includes
#include "OneShotSampleSource.h"
#include "SimpleMultiPlayer.h"

static const char* TAG = "SimpleMultiPlayer";

using namespace oboe;
using namespace parselib;

namespace iolib {

constexpr int32_t kBufferSizeInBursts = 2; // Use 2 bursts as the buffer size (double buffer)

SimpleMultiPlayer::SimpleMultiPlayer()
  : mChannelCount(0), mSampleRate(0), mOutputReset(false)
{}

DataCallbackResult SimpleMultiPlayer::onAudioReady(AudioStream *oboeStream, void *audioData,
        int32_t numFrames) {

    StreamState streamState = oboeStream->getState();
    if (streamState != StreamState::Open && streamState != StreamState::Started) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "  streamState:%d", streamState);
    }
    if (streamState == StreamState::Disconnected) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "  streamState::Disconnected");
    }

    memset(audioData, 0, numFrames * sizeof(float));

    // OneShotSampleSource* sources = mSampleSources.get();
    for(int32_t index = 0; index < mNumSampleBuffers; index++) {
        if (mSampleSources[index]->isPlaying()) {
            mSampleSources[index]->mixAudio((float*)audioData, numFrames);
        }
    }

    return DataCallbackResult::Continue;
}

void SimpleMultiPlayer::onErrorAfterClose(AudioStream *oboeStream, Result error) {
    __android_log_print(ANDROID_LOG_INFO, TAG, "==== onErrorAfterClose() error:%d", error);

    resetAll();
    openStream();
    mOutputReset = true;
}

void SimpleMultiPlayer::onErrorBeforeClose(AudioStream *, Result error) {
    __android_log_print(ANDROID_LOG_INFO, TAG, "==== onErrorBeforeClose() error:%d", error);
}

bool SimpleMultiPlayer::openStream() {
    __android_log_print(ANDROID_LOG_INFO, TAG, "openStream()");

    // Create an audio stream
    AudioStreamBuilder builder;
    builder.setChannelCount(mChannelCount);
    builder.setSampleRate(mSampleRate);
    builder.setCallback(this);
    builder.setPerformanceMode(PerformanceMode::LowLatency);
    builder.setSharingMode(SharingMode::Exclusive);
    builder.setSampleRateConversionQuality(SampleRateConversionQuality::Medium);

    Result result = builder.openManagedStream(mAudioStream);
    if (result != Result::OK){
        return false;
    }

    // Reduce stream latency by setting the buffer size to a multiple of the burst size
    auto setBufferSizeResult = mAudioStream->setBufferSizeInFrames(
            mAudioStream->getFramesPerBurst() * kBufferSizeInBursts);
    if (setBufferSizeResult != Result::OK) {
        return false;
    }

    result = mAudioStream->start();
    if (result != Result::OK){
        return false;
    }

    return true;
}

void SimpleMultiPlayer::setupAudioStream(int32_t channelCount, int32_t sampleRate) {
    __android_log_print(ANDROID_LOG_INFO, TAG, "setupAudioStream()");
    mChannelCount = channelCount;
    mSampleRate = sampleRate;
    mSampleRate = sampleRate;

    openStream();
}

void SimpleMultiPlayer::teardownAudioStream() {
    __android_log_print(ANDROID_LOG_INFO, TAG, "teardownAudioStream()");
    // tear down the player
    if (mAudioStream != nullptr) {
        mAudioStream->stop();
    }
}

void SimpleMultiPlayer::allocSampleData(int32_t numSampleBuffers) {
    mNumSampleBuffers = numSampleBuffers;

    for(int index = 0; index < numSampleBuffers; index++) {
        SampleBuffer* buffer = new SampleBuffer();
        OneShotSampleSource* source = new OneShotSampleSource(buffer);
        mSampleBuffers.push_back(buffer);
        mSampleSources.push_back(source);
    }
}

void SimpleMultiPlayer::loadSampleDataFromAsset(byte* dataBytes, int32_t dataLen, int32_t index) {
    __android_log_print(ANDROID_LOG_INFO, TAG, "loadSampleDataFromAsset()");
    MemInputStream stream(dataBytes, dataLen);

    WavStreamReader reader(&stream);
    reader.parse();

    mSampleBuffers[index]->loadSampleData(&reader);
}

void SimpleMultiPlayer::unloadSampleData() {
    __android_log_print(ANDROID_LOG_INFO, TAG, "unloadSampleData()");
    for (int32_t bufferIndex = 0; bufferIndex < mNumSampleBuffers; bufferIndex++) {
        mSampleBuffers[bufferIndex]->unloadSampleData();
        delete mSampleBuffers[bufferIndex];
        delete mSampleSources[bufferIndex];
    }

    mSampleBuffers.clear();
    mSampleSources.clear();

    mNumSampleBuffers = 0;
}

void SimpleMultiPlayer::triggerDown(int32_t index) {
    if (index < mNumSampleBuffers) {
        mSampleSources[index]->setPlayMode();
    }
}

void SimpleMultiPlayer::triggerUp(int32_t index) {
    if (index < mNumSampleBuffers) {
        mSampleSources[index]->setStopMode();
    }
}

void SimpleMultiPlayer::resetAll() {
    for (int32_t bufferIndex = 0; bufferIndex < mNumSampleBuffers; bufferIndex++) {
        mSampleSources[bufferIndex]->setStopMode();
    }
}

}
