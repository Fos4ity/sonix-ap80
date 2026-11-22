/*
 * Rockbox DSP effects for Sonix: perceptual bass enhancement, tone controls,
 * auditory fatigue reduction and Haas surround, ported to float from Rockbox
 * (https://www.rockbox.org), lib/rbcodec/dsp/{pbe,afr,surround,tone_controls}.c
 *
 * Copyright (C) 2014 by Chiwen Chang (pbe.c, afr.c, surround.c)
 * Copyright (C) 2007 Thom Johansen, (C) 2012 Michael Sevakis (tone_controls.c)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */
#include "src/system/audio/pbe.h"

#include "src/system/core/config.h"

#include <math.h>
#include <string.h>

// Rockbox's delays, in samples, whatever the rate (as there).
#define B3_DLY 72  // ~800 us
#define B2_DLY 180 // ~2000 us
#define B0_DLY 276 // ~3050 us
#define MAXCH 2
#define PASS 512

typedef struct {
	float b0, b1, b2, a1, a2;
	float z1[MAXCH], z2[MAXCH];
} bq_t;

static volatile bool master_on;
static volatile int strength;		// percent
static volatile int precut;		// tenths of a dB
static volatile int bass, treble;	// Rockbox tone controls, dB
static volatile int settings_version = 1;
static int built_version, built_rate;

static float t1, t2, t3;
static bq_t eqf[5], tone;
static float b0buf[MAXCH][B0_DLY + 1], b2buf[MAXCH][B2_DLY + 1], b3buf[MAXCH][B3_DLY + 1];
static int b0h[MAXCH], b2h[MAXCH], b3h[MAXCH];
static int last_b0, last_b2;
static float buf[PASS * MAXCH];

// --- Haas surround state (Rockbox surround.c). Its delays are counted at
// 90.9 samples per ms whatever the rate, exactly as there.
#define DLY_1US 90900
#define MIN_DLY ((DLY_1US * 5) / 1000)
#define MAX_DLY ((DLY_1US * 30) / 1000)
const int surround_delay_table[SURROUND_DELAYS] = {0, 5, 8, 10, 15, 30};
static volatile int sr_delay, sr_balance = 35, sr_fx1 = 3400, sr_fx2 = 320, sr_mix = 50;
static volatile bool sr_side;
static int sr_dly = MAX_DLY, sr_built_dly = -1;
static float sr_tc1, sr_tc2, sr_bc, sr_hc;
static float sr_b0[MAX_DLY / 8 + 1], sr_b2[MAX_DLY + 1], sr_bb[MAX_DLY / 4 + 1], sr_hh[MAX_DLY / 2 + 1],
	sr_cl[MAX_DLY + 1];
static int sr_b0h, sr_b2h, sr_bbh, sr_hhh, sr_clh;

// --- AFR state (Rockbox afr.c)
static volatile int afr_level;
static bq_t afrf[4];

void pbe_init(void) {
	master_on = config_get_int("sound", "rbdsp", 0) != 0;
	strength = (int)config_get_int("sound", "pbe", 0);
	if (strength < 0 || strength > PBE_STRENGTH_MAX) strength = 0;
	precut = (int)config_get_int("sound", "pbe_precut", -25); // Rockbox's default
	if (precut < PBE_PRECUT_MIN || precut > PBE_PRECUT_MAX) precut = -25;
	bass = (int)config_get_int("sound", "tone_bass", 0);
	treble = (int)config_get_int("sound", "tone_treble", 0);
	if (bass < TONE_MIN || bass > TONE_MAX) bass = 0;
	if (treble < TONE_MIN || treble > TONE_MAX) treble = 0;
	int d = (int)config_get_int("sound", "surround_delay", 0), ok = 0;
	for (int i = 0; i < SURROUND_DELAYS; i++) ok |= surround_delay_table[i] == d;
	sr_delay = ok ? d : 0;
	sr_balance = (int)config_get_int("sound", "surround_balance", 35);
	if (sr_balance < 0 || sr_balance > 99) sr_balance = 35;
	sr_fx1 = (int)config_get_int("sound", "surround_fx1", 3400);
	if (sr_fx1 < 600 || sr_fx1 > 8000) sr_fx1 = 3400;
	sr_fx2 = (int)config_get_int("sound", "surround_fx2", 320);
	if (sr_fx2 < 40 || sr_fx2 > 400) sr_fx2 = 320;
	sr_side = config_get_int("sound", "surround_side_only", 0) != 0;
	sr_mix = (int)config_get_int("sound", "surround_mix", 50);
	if (sr_mix < 0 || sr_mix > 100) sr_mix = 50;
	afr_level = (int)config_get_int("sound", "afr", 0);
	if (afr_level < 0 || afr_level > 3) afr_level = 0;
	settings_version++;
}

void rbdsp_set_enabled(bool on) {
	master_on = on;
	config_set_int("sound", "rbdsp", on ? 1 : 0);
	settings_version++;
}
bool rbdsp_enabled(void) { return master_on; }

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
void surround_set_delay(int ms) {
	int ok = 0;
	for (int i = 0; i < SURROUND_DELAYS; i++) ok |= surround_delay_table[i] == ms;
	sr_delay = ok ? ms : 0;
	config_set_int("sound", "surround_delay", sr_delay);
	settings_version++;
}
int surround_get_delay(void) { return sr_delay; }
void surround_set_balance(int v) { sr_balance = clampi(v, 0, 99); config_set_int("sound", "surround_balance", sr_balance); }
int surround_get_balance(void) { return sr_balance; }
void surround_set_fx1(int v) { sr_fx1 = clampi(v, 600, 8000); config_set_int("sound", "surround_fx1", sr_fx1); settings_version++; }
int surround_get_fx1(void) { return sr_fx1; }
void surround_set_fx2(int v) { sr_fx2 = clampi(v, 40, 400); config_set_int("sound", "surround_fx2", sr_fx2); settings_version++; }
int surround_get_fx2(void) { return sr_fx2; }
void surround_set_side_only(bool on) { sr_side = on; config_set_int("sound", "surround_side_only", on ? 1 : 0); }
bool surround_get_side_only(void) { return sr_side; }
void surround_set_mix(int v) { sr_mix = clampi(v, 0, 100); config_set_int("sound", "surround_mix", sr_mix); }
int surround_get_mix(void) { return sr_mix; }
void afr_set(int level) { afr_level = clampi(level, 0, 3); config_set_int("sound", "afr", afr_level); settings_version++; }
int afr_get(void) { return afr_level; }

static int clamp_tone(int v) { return v < TONE_MIN ? TONE_MIN : (v > TONE_MAX ? TONE_MAX : v); }
void tone_set_bass(int db) { bass = clamp_tone(db); config_set_int("sound", "tone_bass", bass); settings_version++; }
void tone_set_treble(int db) { treble = clamp_tone(db); config_set_int("sound", "tone_treble", treble); settings_version++; }
int tone_get_bass(void) { return bass; }
int tone_get_treble(void) { return treble; }

void pbe_set_strength(int percent) {
	if (percent < 0) percent = 0;
	if (percent > PBE_STRENGTH_MAX) percent = PBE_STRENGTH_MAX;
	strength = percent;
	config_set_int("sound", "pbe", percent);
	settings_version++;
}
int pbe_get_strength(void) { return strength; }

void pbe_set_precut(int tenths) {
	if (tenths < PBE_PRECUT_MIN) tenths = PBE_PRECUT_MIN;
	if (tenths > PBE_PRECUT_MAX) tenths = PBE_PRECUT_MAX;
	precut = tenths;
	config_set_int("sound", "pbe_precut", tenths);
	settings_version++;
}
int pbe_get_precut(void) { return precut; }

// Rockbox's peaking filter (RBJ cookbook, Q floored at 0.5).
static void pk(bq_t *f, double fc, double q, double db, double fs) {
	if (q < 0.5) q = 0.5;
	double A = pow(10.0, db / 40.0), w = 2 * M_PI * fc / fs, cs = cos(w), al = sin(w) / (2 * q);
	double a0 = 1 + al / A;
	f->b0 = (float)((1 + al * A) / a0);
	f->b1 = (float)((-2 * cs) / a0);
	f->b2 = (float)((1 - al * A) / a0);
	f->a1 = (float)((-2 * cs) / a0);
	f->a2 = (float)((1 - al / A) / a0);
}

// Rockbox's filter_bishelf_coefs: two first-order shelves cascaded into one
// biquad, with an overall gain on top. Gains in tenths of a dB.
static void bishelf(bq_t *f, double f_lo, double f_hi, double g_lo, double g_hi, double g_all, double fs) {
	double s, c, g, lb0, lb1, la0, la1, hb0, hb1, ha0, ha1;
	g = pow(10.0, g_lo / 400.0);
	s = sin(M_PI * f_lo / fs); c = cos(M_PI * f_lo / fs);
	lb0 = s * g + c; lb1 = s * g - c; la0 = s / g + c; la1 = s / g - c;
	g = pow(10.0, g_hi / 400.0);
	s = sin(M_PI * f_hi / fs); c = cos(M_PI * f_hi / fs);
	hb0 = s + c * g; hb1 = s - c * g; ha0 = s + c / g; ha1 = s - c / g;
	double k = pow(10.0, g_all / 200.0);
	double l0 = k * lb0 / la0, l1 = k * lb1 / la0, lp = la1 / la0; // y = l0 x + l1 x1 - lp y1
	double h0 = hb0 / ha0, h1 = hb1 / ha0, hp = ha1 / ha0;
	f->b0 = (float)(l0 * h0);
	f->b1 = (float)(l0 * h1 + l1 * h0);
	f->b2 = (float)(l1 * h1);
	f->a1 = (float)(lp + hp);
	f->a2 = (float)(lp * hp);
}

static void rebuild(int rate) {
	double fs = rate;
	t1 = (float)(160.0 / fs);
	t2 = (float)(500.0 / fs);
	t3 = (float)(1150.0 / fs);
	bishelf(&eqf[0], 20, 16000, 0, 53, -5 + precut, fs);
	pk(&eqf[1], 64, 2.8, 5.3, fs);
	pk(&eqf[2], 2000, 2.8, 5.8, fs);
	pk(&eqf[3], 7500, 4.3, -8.2, fs);
	pk(&eqf[4], 10000, 4.3, -2.9, fs);
	// Rockbox tone_controls.c: 200 Hz / 3.5 kHz shelves, prescaled down by the
	// larger boost so a boost cannot clip.
	int pre = bass > treble ? bass : treble;
	if (pre < 0) pre = 0;
	bishelf(&tone, 200, 3500, bass * 10, treble * 10, -pre * 10, fs);
	// surround.c surround_update_filter: plain ratios, used as gains.
	sr_tc1 = (float)(sr_fx2 / fs);
	sr_tc2 = (float)(sr_fx1 / fs);
	sr_bc = (float)((sr_fx2 / 2) / fs);
	sr_hc = (float)((sr_fx1 * 2) / fs);
	int dly = (DLY_1US * sr_delay) / 1000;
	sr_dly = dly < MIN_DLY ? MIN_DLY : (dly > MAX_DLY ? MAX_DLY : dly);
	if (sr_dly != sr_built_dly || rate != built_rate) {
		memset(sr_b0, 0, sizeof(sr_b0)); memset(sr_b2, 0, sizeof(sr_b2)); memset(sr_bb, 0, sizeof(sr_bb));
		memset(sr_hh, 0, sizeof(sr_hh)); memset(sr_cl, 0, sizeof(sr_cl));
		sr_b0h = sr_b2h = sr_bbh = sr_hhh = sr_clh = 0;
		sr_built_dly = sr_dly;
	}
	// afr.c strength_update (gains in tenths of a dB, Q in tenths).
	int hs = 0, ls = 0, drop3k = 0;
	switch (afr_level) {
	case 1: hs = -29; ls = 0; drop3k = -13; break;
	case 2: hs = -58; ls = -5; drop3k = -29; break;
	case 3: hs = -72; ls = -5; drop3k = -48; break;
	}
	bishelf(&afrf[0], 8000, 20000, 0, hs / 2, 0, fs);
	pk(&afrf[1], 8000, 2.8, hs / 10.0, fs);
	pk(&afrf[2], 3150, 2.8, drop3k / 10.0, fs);
	bishelf(&afrf[3], 200, 1280, ls, 0, 0, fs);
	if (rate != built_rate) {
		for (int i = 0; i < 4; i++) memset(afrf[i].z1, 0, sizeof(afrf[i].z1)), memset(afrf[i].z2, 0, sizeof(afrf[i].z2));
		memset(tone.z1, 0, sizeof(tone.z1)); memset(tone.z2, 0, sizeof(tone.z2));
		for (int i = 0; i < 5; i++) memset(eqf[i].z1, 0, sizeof(eqf[i].z1)), memset(eqf[i].z2, 0, sizeof(eqf[i].z2));
		memset(b0buf, 0, sizeof(b0buf)); memset(b2buf, 0, sizeof(b2buf)); memset(b3buf, 0, sizeof(b3buf));
		memset(b0h, 0, sizeof(b0h)); memset(b2h, 0, sizeof(b2h)); memset(b3h, 0, sizeof(b3h));
	}
	built_rate = rate;
}

// A ring of `len` samples read then written at one head: a delay of len.
static inline float ring(float *b, int *h, int len, float x) {
	if (len <= 0) return x;
	if (*h >= len) *h = 0;
	float y = b[*h];
	b[*h] = x;
	if (++*h >= len) *h = 0;
	return y;
}

static void biquad(bq_t *f, int n, int ch) {
	for (int c = 0; c < ch; c++) {
		float z1 = f->z1[c], z2 = f->z2[c];
		for (int i = 0; i < n; i++) {
			float x = buf[i * ch + c];
			float y = f->b0 * x + z1;
			z1 = f->b1 * x - f->a1 * y + z2;
			z2 = f->b2 * x - f->a2 * y;
			buf[i * ch + c] = y;
		}
		f->z1[c] = z1; f->z2[c] = z2;
	}
}

static void surround_run(int n) {
	int d = sr_dly, d3 = d / 8, d2 = d / 4, d1 = d / 2;
	float tc1 = sr_tc1, tc2 = sr_tc2, bc = sr_bc, hc = sr_hc;
	bool side_only = sr_side;
	int bal = sr_balance, mix = sr_mix;
	for (int i = 0; i < n; i++) {
		float l = buf[i * 2], r = buf[i * 2 + 1];
		float mid = l / 2 + r / 2, side = l - r, t0, t1, x;
		if (!side_only) {
			t0 = l;							 // clone the left channel
			t1 = r * tc1 - r * tc2;			 // keep the middle band of the right
		} else {
			t0 = side / 2;
			t1 = (-side * tc1) / 2 - (-side * tc2) / 2;
		}
		x = t1 * 0.35f;						 // inverted crossfeed delay to widen
		t0 += ring(sr_cl, &sr_clh, d, -x);
		x = r - r * tc1;					 // 1/8 delay below fx2
		t1 += ring(sr_b0, &sr_b0h, d3, x);
		t1 = t1 * bc;						 // cut below half fx2
		x = r - r * bc;						 // 1/4 delay below half fx2
		t1 += ring(sr_bb, &sr_bbh, d2, x);
		x = r * tc2;						 // full delay to the higher band
		t1 += ring(sr_b2, &sr_b2h, d, x);
		t1 -= t1 * hc;
		x = r * hc;
		t1 += ring(sr_hh, &sr_hhh, d1, x);
		if (bal > 0 && !side_only) {
			t0 -= t0 * bal / 200.0f;
			t1 += t1 * bal / 200.0f;
		} else if (bal > 0) {
			t0 += t0 * bal / 200.0f;
			t1 -= t1 * bal / 200.0f;
		}
		if (side_only) {
			t0 += mid;
			t1 += mid;
		}
		buf[i * 2] = (l * (100 - mix) + t0 * mix) / 100.0f;
		buf[i * 2 + 1] = (r * (100 - mix) + t1 * mix) / 100.0f;
	}
}

static void pbe_run(int n, int ch);

// Rockbox's order: tone controls, PBE, AFR, surround.
static void run(int n, int ch) {
	if (bass || treble) biquad(&tone, n, ch);
	pbe_run(n, ch);
	if (afr_level > 0)
		for (int k = 0; k < 4; k++) biquad(&afrf[k], n, ch);
	if (sr_delay > 0 && ch == 2) surround_run(n);
}

static void pbe_run(int n, int ch) {
	int s = strength;
	if (s <= 0) return;
	int l0 = (B0_DLY * s) / 100, l2 = (B2_DLY * s) / 100;
	if (l0 != last_b0 || l2 != last_b2) {
		// The delay changed length: start the rings over rather than read stale taps.
		memset(b0buf, 0, sizeof(b0buf)); memset(b2buf, 0, sizeof(b2buf));
		memset(b0h, 0, sizeof(b0h)); memset(b2h, 0, sizeof(b2h));
		last_b0 = l0; last_b2 = l2;
	}
	for (int c = 0; c < ch; c++) {
		for (int i = 0; i < n; i++) {
			float x = buf[i * ch + c];
			float m1 = x * t1, m2 = x * t2, m3 = x * t3;
			float y = m1 - m2;									 // 160-500 Hz share, no delay
			y += ring(b0buf[c], &b0h[c], l0, x - m1);			 // below 160 Hz
			y += ring(b2buf[c], &b2h[c], l2, m2 - m3);			 // 500-1150 Hz
			y += ring(b3buf[c], &b3h[c], B3_DLY, m3);			 // above 1150 Hz
			buf[i * ch + c] = y;
		}
	}
	for (int k = 0; k < 5; k++) biquad(&eqf[k], n, ch);
}

static bool prepare(int frames, int ch, int rate) {
	if (!master_on || (strength <= 0 && !bass && !treble && afr_level <= 0 && sr_delay <= 0) || frames <= 0 || rate <= 0 || ch < 1 || ch > MAXCH) return false;
	if (built_version != settings_version || built_rate != rate) {
		built_version = settings_version;
		rebuild(rate);
	}
	return true;
}

void pbe_process(short *frames, int frame_count, int channels, int sample_rate) {
	if (!prepare(frame_count, channels, sample_rate)) return;
	for (int done = 0; done < frame_count; done += PASS) {
		int n = frame_count - done < PASS ? frame_count - done : PASS;
		short *in = frames + (size_t)done * channels;
		for (int i = 0; i < n * channels; i++) buf[i] = in[i];
		run(n, channels);
		for (int i = 0; i < n * channels; i++) {
			float x = buf[i];
			if (x > 32767.0f) x = 32767.0f;
			if (x < -32768.0f) x = -32768.0f;
			in[i] = (short)(x >= 0 ? x + 0.5f : x - 0.5f);
		}
	}
}

void pbe_process_s32(int32_t *frames, int frame_count, int channels, int sample_rate) {
	if (!prepare(frame_count, channels, sample_rate)) return;
	const float down = 1.0f / 65536.0f;
	for (int done = 0; done < frame_count; done += PASS) {
		int n = frame_count - done < PASS ? frame_count - done : PASS;
		int32_t *in = frames + (size_t)done * channels;
		for (int i = 0; i < n * channels; i++) buf[i] = (float)in[i] * down;
		run(n, channels);
		for (int i = 0; i < n * channels; i++) {
			float x = buf[i];
			if (x > 32767.0f) x = 32767.0f;
			if (x < -32768.0f) x = -32768.0f;
			in[i] = (int32_t)(x * 65536.0f);
		}
	}
}
