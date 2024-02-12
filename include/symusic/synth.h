//
// Created by nhy on 2024/2/12.
//
#pragma once
#ifndef SYNTH_H
#define SYNTH_H

#include "prestosynth/synthesizer.h"
#include "symusic/score.h"


namespace symusic {
class Synthesizer {
private:
    psynth::Synthesizer synthesizer;

public:
    Synthesizer(const std::string &sfPath, uint32_t sampleRate, uint8_t quality, uint8_t worker_num = 1):
        synthesizer(sfPath, sampleRate, quality, worker_num) {}

    template<TType T>
    psynth::AudioData render(const Score<T> & score, bool stereo);
};

}
#endif //SYNTH_H
