#include "src/system/audio/crossfade.h"

#include <math.h>

int crossfade_clamp_seconds(int seconds) {
	if (seconds < CROSSFADE_MIN_SECONDS) {
		return CROSSFADE_MIN_SECONDS;
	}
	if (seconds > CROSSFADE_MAX_SECONDS) {
		return CROSSFADE_MAX_SECONDS;
	}
	return seconds;
}

int crossfade_phase_q15(double elapsed_secs, double overlap_secs) {
	if (overlap_secs <= 0.0) {
		return 0;
	}
	double t = elapsed_secs / overlap_secs;
	if (t <= 0.0) {
		return 0;
	}
	if (t >= 1.0) {
		return CROSSFADE_ONE_Q15;
	}
	return (int)(t * CROSSFADE_ONE_Q15 + 0.5);
}

void crossfade_gains_q15(int phase_q15, int *out_gain, int *in_gain) {
	if (phase_q15 < 0) {
		phase_q15 = 0;
	}
	if (phase_q15 > CROSSFADE_ONE_Q15) {
		phase_q15 = CROSSFADE_ONE_Q15;
	}
	double t = (double)phase_q15 / CROSSFADE_ONE_Q15;
	double angle = t * (M_PI / 2.0);
	*out_gain = (int)(cos(angle) * CROSSFADE_ONE_Q15 + 0.5);
	*in_gain = (int)(sin(angle) * CROSSFADE_ONE_Q15 + 0.5);
	if (*out_gain < 0) {
		*out_gain = 0;
	}
	if (*in_gain < 0) {
		*in_gain = 0;
	}
	if (*out_gain > CROSSFADE_ONE_Q15) {
		*out_gain = CROSSFADE_ONE_Q15;
	}
	if (*in_gain > CROSSFADE_ONE_Q15) {
		*in_gain = CROSSFADE_ONE_Q15;
	}
}

bool crossfade_should_apply(const crossfade_config_t *cfg, const crossfade_stream_t *out, const crossfade_stream_t *in) {
	if (!cfg || !out || !in) {
		return false;
	}
	if (!cfg->enabled || !cfg->gapless) {
		return false;
	}
	if (cfg->seconds < CROSSFADE_MIN_SECONDS || cfg->seconds > CROSSFADE_MAX_SECONDS) {
		return false;
	}
	if (out->dsd || in->dsd || out->bluetooth || in->bluetooth) {
		return false;
	}
	if (out->sample_rate != in->sample_rate || out->bits != in->bits || out->channels != in->channels) {
		return false;
	}
	return true;
}

void crossfade_mix_s16(short *dst, const short *out, const short *in, int samples, int out_gain_q15, int in_gain_q15) {
	for (int i = 0; i < samples; i++) {
		long long v = ((long long)out[i] * out_gain_q15 + (long long)in[i] * in_gain_q15) >> 15;
		if (v > 32767) {
			v = 32767;
		} else if (v < -32768) {
			v = -32768;
		}
		dst[i] = (short)v;
	}
}

void crossfade_mix_s32(int32_t *dst, const int32_t *out, const int32_t *in, int samples, int out_gain_q15, int in_gain_q15) {
	for (int i = 0; i < samples; i++) {
		long long v = ((long long)out[i] * out_gain_q15 + (long long)in[i] * in_gain_q15) >> 15;
		if (v > 2147483647LL) {
			v = 2147483647LL;
		} else if (v < -2147483648LL) {
			v = -2147483648LL;
		}
		dst[i] = (int32_t)v;
	}
}
