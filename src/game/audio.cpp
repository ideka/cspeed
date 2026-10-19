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

#include <atomic>
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

// Music after a pause (enhancement): continue the song exactly where it was paused.
//
// Pausing a race stops the music (func_80055B4C, alSeqpStop), which ends the notes that are playing, and
// continuing asks for the race's song again (func_80055C28), which starts it over. Picking the sequence
// up where it stopped is not enough: songs have long recorded parts (a guitar solo) started by a single
// note, which the stop ended. So while racing (game state 3, also while paused) the stop freezes the
// music instead: the sequence player's handler (func_80005FA4) does nothing, so its clock and every
// pending event (note ends, envelopes) wait, and the voices it plays are left out of the mix
// (alEnvmixerPull), so their samples, envelopes and filters stay where they are. Sound effects play on
// their own voices and are not affected.
//
// Asking for the same song while still racing (continue) unfreezes it. Anything else unfreezes it and
// stops it for real: another stop, another song, or the same song outside the race (restarting it). A
// request stops it and returns "not yet" (0), which makes the game ask again until the song is stopped
// and the new one loaded (or the same one reloaded, so it starts over). The stop a song request does
// itself to change songs always stops.
namespace {

constexpr gpr GAME_STATE = (gpr)(int32_t)0x8016D928;
constexpr int32_t STATE_RACING = 3;
constexpr gpr LOADED_SONG = (gpr)(int32_t)0x80141DDC;     // u8, 0xFF for none
constexpr int32_t NO_SONG = 0xFF;
constexpr gpr MUSIC_OFF = (gpr)(int32_t)0x80022D20;       // nonzero: the music functions do nothing
constexpr gpr MUSIC_PLAYER = (gpr)(int32_t)0x80142730;    // ALSeqPlayer*

// ALSeqPlayer, ALVoiceState and ALEnvMixer fields (libultra).
constexpr int32_t SEQP_STATE = 0x2C;          // AL_PLAYING = 1
constexpr int32_t SEQP_FRAME_TIME = 0x5C;     // microseconds
constexpr int32_t SEQP_VOICES = 0x64;         // ALVoiceState* (allocated voices)
constexpr int32_t VSTATE_PVOICE = 0x0C;       // voice.pvoice
constexpr int32_t PVOICE_ENVMIXER = 0x0C;     // channelKnob
constexpr int32_t ENVMIXER_CTRL_LIST = 0x3C;
constexpr int32_t AL_PLAYING = 1;
constexpr int MAX_VOICES = 64;

std::atomic<bool> music_frozen = false;
std::atomic<bool> song_change = false;  // the stop is the song request's own

gpr music_player(uint8_t* rdram) {
    return (gpr)MEM_W(0, MUSIC_PLAYER);
}

bool racing(uint8_t* rdram) {
    return MEM_W(0, GAME_STATE) == STATE_RACING;
}

int32_t loaded_song(uint8_t* rdram) {
    return (int32_t)(uint8_t)MEM_BU(0, LOADED_SONG);
}

} // namespace

extern "C" void func_80005DF0(uint8_t* rdram, recomp_context* ctx);  // alSeqpStop

// Start of func_80055B4C (stop the music): true to return without stopping it.
extern "C" int cspeed_music_stop(uint8_t* rdram, recomp_context* ctx) {
    if (music_frozen.exchange(false) || song_change.exchange(false)) {
        return false;  // stopped for real
    }
    if (!cspeed::resume_music_after_pause() || !racing(rdram) || MEM_W(0, MUSIC_OFF) != 0 ||
        loaded_song(rdram) == NO_SONG || MEM_W(SEQP_STATE, music_player(rdram)) != AL_PLAYING) {
        return false;
    }
    music_frozen = true;
    return true;
}

// Start of func_80055C28 (ask for a song, $a0): true to return $v0 without the rest.
extern "C" int cspeed_music_request(uint8_t* rdram, recomp_context* ctx) {
    if (!music_frozen.exchange(false)) {
        return false;
    }
    if ((int32_t)ctx->r4 == loaded_song(rdram) && racing(rdram)) {
        ctx->r2 = 2;  // continue: the song is playing already
        return true;
    }
    ctx->r4 = music_player(rdram);
    func_80005DF0(rdram, ctx);
    ctx->r2 = 0;
    return true;
}

// In func_80055C28, before it stops the song that is playing to play another one.
extern "C" void cspeed_music_song_change() {
    song_change = true;
}

// Start of the sequence player's handler ($a0 = player): true to return $v0 (the time until the next
// call) without playing anything.
extern "C" int cspeed_music_player_frozen(uint8_t* rdram, recomp_context* ctx) {
    if (!music_frozen || ctx->r4 != music_player(rdram)) {
        return false;
    }
    ctx->r2 = MEM_W(SEQP_FRAME_TIME, ctx->r4);
    return true;
}

// Start of alEnvmixerPull ($a0 = envelope mixer): true to leave the voice out of this frame. Only when
// it has no parameter updates for the frame (a voice taken for a sound effect gets them), since those
// are due at this frame's sample times.
extern "C" int cspeed_music_voice_frozen(uint8_t* rdram, recomp_context* ctx) {
    if (!music_frozen || MEM_W(ENVMIXER_CTRL_LIST, ctx->r4) != 0) {
        return false;
    }
    gpr state = (gpr)MEM_W(SEQP_VOICES, music_player(rdram));
    for (int i = 0; i < MAX_VOICES && state != 0; i++) {
        gpr pvoice = (gpr)MEM_W(VSTATE_PVOICE, state);
        if (pvoice != 0 && (gpr)MEM_W(PVOICE_ENVMIXER, pvoice) == ctx->r4) {
            return true;
        }
        state = (gpr)MEM_W(0, state);
    }
    return false;
}
