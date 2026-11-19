/*
 * Host test for the crossfade engine (src/system/audio/crossfade.c).
 *
 * Two things are pinned here. The equal-power envelope has to start at the
 * outgoing stream, reach equal power in the middle and end at the incoming
 * stream. The decision to crossfade has to reject every stream pair the audio
 * path cannot mix, and accept one it can.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "src/system/audio/crossfade.h"

static int failures;

static void check(bool ok, const char *what) {
	if (ok) {
		printf("ok - %s\n", what);
	} else {
		printf("FAIL - %s\n", what);
		failures++;
	}
}

static bool near(int got, int want, int tolerance) {
	return abs(got - want) <= tolerance;
}

static crossfade_config_t good_config(void) {
	crossfade_config_t cfg = {true, true, 6};
	return cfg;
}

static crossfade_stream_t good_stream(void) {
	crossfade_stream_t s = {44100, 16, 2, false, false};
	return s;
}

static void test_envelope(void) {
	int out_gain, in_gain;

	crossfade_gains_q15(0, &out_gain, &in_gain);
	check(out_gain == 32768 && in_gain == 0, "envelope start: full outgoing, silent incoming");

	crossfade_gains_q15(32768, &out_gain, &in_gain);
	check(out_gain == 0 && in_gain == 32768, "envelope end: silent outgoing, full incoming");

	crossfade_gains_q15(16384, &out_gain, &in_gain);
	check(near(out_gain, 23170, 2) && near(in_gain, 23170, 2), "envelope middle: equal power in both arms");

	bool constant = true;
	for (int phase = 0; phase <= 32768; phase += 2048) {
		crossfade_gains_q15(phase, &out_gain, &in_gain);
		double power = (double)out_gain * out_gain + (double)in_gain * in_gain;
		long long want = 32768LL * 32768LL;
		if (llabs((long long)power - want) > 2 * 32768) {
			constant = false;
		}
	}
	check(constant, "envelope keeps constant power across the overlap");
}

static void test_phase(void) {
	check(crossfade_phase_q15(3.0, 6.0) == 16384, "phase maps the midpoint to Q15 half");
	check(crossfade_phase_q15(-1.0, 6.0) == 0, "phase clamps below zero");
	check(crossfade_phase_q15(9.0, 6.0) == 32768, "phase clamps past the overlap");
}

static void test_decision(void) {
	crossfade_config_t cfg = good_config();
	crossfade_stream_t out = good_stream();
	crossfade_stream_t in = good_stream();

	check(crossfade_should_apply(&cfg, &out, &in), "matching local PCM pair crossfades");

	cfg.enabled = false;
	check(!crossfade_should_apply(&cfg, &out, &in), "crossfade off rejects");
	cfg = good_config();

	cfg.gapless = false;
	check(!crossfade_should_apply(&cfg, &out, &in), "gapless off rejects");
	cfg = good_config();

	cfg.seconds = 0;
	check(!crossfade_should_apply(&cfg, &out, &in), "out of range duration rejects");
	cfg = good_config();

	out.dsd = true;
	check(!crossfade_should_apply(&cfg, &out, &in), "DSD outgoing rejects");
	out = good_stream();
	in.dsd = true;
	check(!crossfade_should_apply(&cfg, &out, &in), "DSD incoming rejects");
	in = good_stream();

	out.bluetooth = true;
	check(!crossfade_should_apply(&cfg, &out, &in), "Bluetooth rejects");
	out = good_stream();

	in.sample_rate = 48000;
	check(!crossfade_should_apply(&cfg, &out, &in), "sample rate mismatch rejects");
	in = good_stream();

	in.bits = 24;
	check(!crossfade_should_apply(&cfg, &out, &in), "bit depth mismatch rejects");
	in = good_stream();

	in.channels = 1;
	check(!crossfade_should_apply(&cfg, &out, &in), "channel count mismatch rejects");
}

static void test_mix(void) {
	short out_s16[4] = {100, -200, 300, -400};
	short in_s16[4] = {1000, 1000, 1000, 1000};
	short dst_s16[4];

	crossfade_mix_s16(dst_s16, out_s16, in_s16, 4, 32768, 0);
	check(dst_s16[0] == 100 && dst_s16[1] == -200, "mix at the start keeps the outgoing samples");

	crossfade_mix_s16(dst_s16, out_s16, in_s16, 4, 0, 32768);
	check(dst_s16[2] == 1000 && dst_s16[3] == 1000, "mix at the end keeps the incoming samples");

	int32_t out_s32[2] = {1 << 20, -(1 << 20)};
	int32_t in_s32[2] = {0, 0};
	int32_t dst_s32[2];
	crossfade_mix_s32(dst_s32, out_s32, in_s32, 2, 23170, 0);
	check(near(dst_s32[0], 741440, 2) && near(dst_s32[1], -741440, 2), "S32 mix scales the outgoing arm by Q15 gain");
	crossfade_mix_s32(dst_s32, out_s32, in_s32, 2, 0, 32768);
	check(dst_s32[0] == 0 && dst_s32[1] == 0, "S32 mix applies the incoming arm");

	check(crossfade_clamp_seconds(0) == 1 && crossfade_clamp_seconds(99) == 12 && crossfade_clamp_seconds(6) == 6,
		  "duration clamps into 1..12 seconds");
}

int main(void) {
	test_envelope();
	test_phase();
	test_decision();
	test_mix();
	printf("%s\n", failures == 0 ? "ALL PASS" : "FAILURES");
	return failures == 0 ? 0 : 1;
}
