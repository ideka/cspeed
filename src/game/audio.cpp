// Audio sample rate.
//
// The game uses the stock libultra synthesizer and mixes at 22050 Hz: its audio setup (func_80056A78)
// calls osAiSetFrequency(22050) and sizes everything else from the rate that call returns (frame size,
// output buffers). The sound effects bank is 44100 Hz, so a higher rate keeps their treble and avoids
// aliasing when samples are pitched up. The call is hooked (cspeed.us.toml) to ask for the rate chosen
// in the sound options.
//
// Two things assume 22050 Hz and are scaled to sound the same at the new rate: the voices' pitches
// (1.0 plays a sample at 22050 Hz), in the resampler, and the reverb (libultra's big room preset),
// whose delays are in samples, before alInit reads it.

#include <cmath>
#include <cstdio>
#include <cstring>

#include "recomp.h"
#include "cspeed_config.h"

namespace {

constexpr uint32_t ORIGINAL_RATE = 22050;
constexpr gpr BIG_ROOM_PARAMS = (gpr)(int32_t)0x8001AD88;  // libultra BIG_ROOM_PARAMS
constexpr int32_t BIG_ROOM_SECTIONS = 4;
constexpr int32_t BIG_ROOM_LENGTH = 4000;
constexpr int SECTION_WORDS = 8;  // input, output, fbcoef, ffcoef, gain, chorus rate, chorus depth, low-pass coef
constexpr gpr AUDIO_HEAP = (gpr)(int32_t)0x8014FD58;       // ALHeap: base, cur, len, count

// The rate the game was started with (changing the option takes effect on the next start).
uint32_t active_rate = ORIGINAL_RATE;

// Delays and lengths are multiples of 8 samples, like the presets' own values.
int32_t scale_samples(int32_t samples, double ratio) {
    return (int32_t)std::lround(samples * ratio / 8.0) * 8;
}

void scale_reverb(uint8_t* rdram, uint32_t rate) {
    if (MEM_W(0, BIG_ROOM_PARAMS) != BIG_ROOM_SECTIONS || MEM_W(4, BIG_ROOM_PARAMS) != BIG_ROOM_LENGTH) {
        printf("Audio: reverb preset not found, left as is\n");
        return;
    }

    double ratio = (double)rate / ORIGINAL_RATE;
    MEM_W(4, BIG_ROOM_PARAMS) = scale_samples(BIG_ROOM_LENGTH, ratio);
    for (int section = 0; section < BIG_ROOM_SECTIONS; section++) {
        int32_t offset = 8 + section * SECTION_WORDS * 4;
        MEM_W(offset + 0, BIG_ROOM_PARAMS) = scale_samples(MEM_W(offset + 0, BIG_ROOM_PARAMS), ratio);
        MEM_W(offset + 4, BIG_ROOM_PARAMS) = scale_samples(MEM_W(offset + 4, BIG_ROOM_PARAMS), ratio);
        // One-pole low-pass (y += coef * (y[n-1] - x), coef in Q15): keep the same cutoff frequency.
        int32_t coef = MEM_W(offset + 28, BIG_ROOM_PARAMS);
        if (coef > 0) {
            MEM_W(offset + 28, BIG_ROOM_PARAMS) = (int32_t)std::lround(32768.0 * std::pow(coef / 32768.0, 1.0 / ratio));
        }
    }
}

} // namespace

// Called right before the game's osAiSetFrequency call, with the requested rate in $a0.
extern "C" void cspeed_set_audio_rate(uint8_t* rdram, recomp_context* ctx) {
    uint32_t rate = cspeed::audio_sample_rate();
    if (rate == ORIGINAL_RATE) {
        return;
    }
    active_rate = rate;
    ctx->r4 = rate;
    scale_reverb(rdram, rate);
}

// Called when a voice's pitch is set in the resampler ($a2 = pitch as a float). The game computes
// pitches for 22050 Hz output (1.0 plays a sample at 22050 Hz), so scale them to the actual rate.
extern "C" void cspeed_audio_pitch(uint8_t* rdram, recomp_context* ctx) {
    if (active_rate == ORIGINAL_RATE) {
        return;
    }
    uint32_t bits = (uint32_t)ctx->r6;
    float pitch;
    memcpy(&pitch, &bits, sizeof(pitch));
    pitch *= (float)ORIGINAL_RATE / active_rate;
    memcpy(&bits, &pitch, sizeof(bits));
    ctx->r6 = (gpr)(int32_t)bits;
}

// Called at the end of the audio setup.
extern "C" void cspeed_after_audio_init(uint8_t* rdram, recomp_context* ctx) {
    int32_t used = MEM_W(4, AUDIO_HEAP) - MEM_W(0, AUDIO_HEAP);
    int32_t size = MEM_W(8, AUDIO_HEAP);
    printf("Audio: %u Hz, audio heap %d of %d bytes used\n", cspeed::audio_sample_rate(), used, size);
}
