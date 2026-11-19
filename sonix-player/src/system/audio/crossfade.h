#ifndef CROSSFADE_H
#define CROSSFADE_H

#include <stdbool.h>
#include <stdint.h>

// True crossfade: the tail of one track overlaps the head of the next. Two
// independent envelopes cross in the overlap: the outgoing arm falls from
// unity to silence while the incoming arm rises from silence to unity. They
// are equal power, so the sum stays at the level of either arm alone (cos^2 +
// sin^2 = 1). Gains are Q15 (32768 = unity) and the per-sample mixer is integer
// only; the trigonometry runs once per buffer, not once per sample.
//
// This is deliberately separate from the old "dissolvenza" sequential fade in
// audio.c. That one ramps a single decoder down and the next up with no
// overlap, and it stays available for streams this module cannot mix.

#define CROSSFADE_MIN_SECONDS 1
#define CROSSFADE_MAX_SECONDS 12
#define CROSSFADE_ONE_Q15 32768

typedef struct {
	bool enabled; // the user's crossfade switch
	bool gapless; // crossfade reuses the gapless PCM hold; off means no overlap
	int seconds;  // length of the overlap, 1..12
} crossfade_config_t;

typedef struct {
	int sample_rate; // Hz
	int bits;		 // source depth: 16, 24 or 32
	int channels;
	bool dsd;		// DoP markers cannot be mixed
	bool bluetooth; // a2dp link, not a mixable local PCM stream
} crossfade_stream_t;

// Clamp a configured duration into the supported 1..12 second range.
int crossfade_clamp_seconds(int seconds);

// Progress through an overlap, in Q15, clamped to [0, 32768].
int crossfade_phase_q15(double elapsed_secs, double overlap_secs);

// Equal-power gains for a phase in [0, 32768].
// *out_gain = cos(phase/32768 * pi/2), *in_gain = sin(phase/32768 * pi/2).
void crossfade_gains_q15(int phase_q15, int *out_gain, int *in_gain);

// Whether a transition from `out` into `in` can be crossfaded under `cfg`.
// False for a disabled switch, a missing gapless hold, an out of range
// duration, DSD or Bluetooth, or a format mismatch on rate, depth or channels.
bool crossfade_should_apply(const crossfade_config_t *cfg, const crossfade_stream_t *out, const crossfade_stream_t *in);

// Blend two interleaved buffers into `dst`: dst = out*out_gain + in*in_gain,
// with both gains in Q15. `samples` counts individual samples, not frames.
void crossfade_mix_s16(short *dst, const short *out, const short *in, int samples, int out_gain_q15, int in_gain_q15);
void crossfade_mix_s32(int32_t *dst, const int32_t *out, const int32_t *in, int samples, int out_gain_q15, int in_gain_q15);

#endif // CROSSFADE_H
